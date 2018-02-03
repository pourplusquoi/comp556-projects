#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <set>
#include "packheader.h"

#define RECV_SIZE 10024
#define SEND_SIZE 12
#define WINDOW_SIZE 100
#define PACKET_SIZE 10000

using std::cout;
using std::endl;
using std::ofstream;
using std::string;
using std::map;
using std::set;
using std::to_string;

void pack_up (char *dst, char *src, PackHeader *header) {
    header->crc32_val = crc32 (src + 4, 8);
    memcpy (dst, src, 12);
}

void recv_file_dir (const int sock, char *send_buffer, char *recv_buffer, 
    struct sockaddr_in &send_addr, socklen_t &send_addr_len, string &subdir, string &filename) {

    std::ios::sync_with_stdio(false);
    
    bool ack = false;
    while (true) {

        if (recvfrom (sock, recv_buffer, RECV_SIZE, 0, 
            (struct sockaddr *) &send_addr, &send_addr_len) < 0) {
            if (ack) {
                while (sendto (sock, send_buffer, 12, 0, 
                    (struct sockaddr *) &send_addr, send_addr_len) < 0);
            }
            continue;
        }

        PackHeader *header = (PackHeader *) (recv_buffer);

        if (header->packet_size < RECV_SIZE && header->packet_size > 4 && 
            header->crc32_val == crc32 (recv_buffer + 4, header->packet_size - 4)) {

            if (header->serial_id > 0)
                break;

            ack = true;

            string file_dir = recv_buffer + 12;
            int index = file_dir.find('\n');
            subdir = file_dir.substr(0, index);
            filename = file_dir.substr(index + 1) + ".recv";

            pack_up(send_buffer, recv_buffer, header);
            while (sendto (sock, send_buffer, 12, 0, 
                (struct sockaddr *) &send_addr, send_addr_len) < 0);
        }
    }
}

void recv_file_content (int sock, char *send_buffer, char *recv_buffer,
    struct sockaddr_in &send_addr, socklen_t &send_addr_len, ofstream &ofs) {

    std::ios::sync_with_stdio(false);

    unsigned int window_lo = 1, window_hi = WINDOW_SIZE, serial_end = 4294967295;

    set <int> recvieved;
    map <unsigned int, string> cache;

    while (true) {

        if (recvfrom (sock, recv_buffer, RECV_SIZE, 0, 
            (struct sockaddr *) &send_addr, &send_addr_len) < 0)
            continue;

        PackHeader *header = (PackHeader *) (recv_buffer);

        if (header->serial_id == 0)
            continue;

        if (header->packet_size < RECV_SIZE && header->packet_size > 4 && 
            header->crc32_val == crc32 (recv_buffer + 4, header->packet_size - 4)) {

            if (header->serial_id < serial_end && header->serial_id > 0) {
                cout << "[recv data] " << (header->serial_id - 1) * PACKET_SIZE;
                cout << " (" << header->packet_size - 12 << ") ";
            }

            // inside window and not exist :: accept
            if (header->serial_id >= window_lo && header->serial_id <= window_hi && 
                cache.find (header->serial_id) == cache.end ()) {

                cache[header->serial_id] = string (recv_buffer + 12, header->packet_size - 12);
                recvieved.insert (header->serial_id);

                for (unsigned int i = window_lo; i <= window_hi && 
                    recvieved.find (i) != recvieved.end (); i++) {

                    ofs << cache[i];
                    cache.erase(i);
                    recvieved.erase(i);
                    window_lo++, window_hi++;
                }
                ofs.flush();

                if (window_lo == header->serial_id + 1)
                    cout << "ACCEPTED(in-order)" << endl;
                else cout << "ACCEPTED(out-of-order)" << endl;

                header->serial_id = window_lo - 1;
                pack_up(send_buffer, recv_buffer, header);
                
                sendto (sock, send_buffer, 12, 0, 
                    (struct sockaddr *) &send_addr, send_addr_len);
            }

            // transmission terminates
            else if (header->serial_id == serial_end) {
                pack_up(send_buffer, recv_buffer, header);
                
                sendto (sock, send_buffer, 12, 0, 
                    (struct sockaddr *) &send_addr, send_addr_len);
                
                break;
            }

            // outside window or already exist :: reject
            else {
                header->serial_id = window_lo - 1;
                pack_up(send_buffer, recv_buffer, header);
                
                cout << "IGNORED" << endl;
                
                sendto (sock, send_buffer, 12, 0, 
                    (struct sockaddr *) &send_addr, send_addr_len);
            }
        }
        else cout << "[recv corrupt packet]" << endl;
    }

    // end of transmission
    cout << "[completed]" << endl;
}

int main (int argc, char** argv) {

    std::ios::sync_with_stdio(false);

    int sock;
    struct sockaddr_in recv_addr;

    if (argc != 3) {
        printf("wrong input format\n");
        printf("./recvfile -p <port>\n");
        exit(1);
    }
    
    unsigned short recv_port = atoi (argv[2]);

    string subdir, filename;

    char recv_buffer[RECV_SIZE], send_buffer[SEND_SIZE];
    memset(recv_buffer, 0, RECV_SIZE);
    memset(send_buffer, 0, SEND_SIZE);
    
    memset (&recv_addr, 0, sizeof (recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    recv_addr.sin_port = htons (recv_port);

    if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("opening UDP socket");
        exit(1);
    }

    if (bind (sock, (struct sockaddr *) &recv_addr, sizeof (recv_addr)) < 0) {
        perror ("binding socket to address");
        exit (1);
    }

    if (fcntl (sock, F_SETFL, O_NONBLOCK) < 0) {
        perror ("making socket non-blocking");
        exit (1);
    }

    struct sockaddr_in send_addr;
    socklen_t send_addr_len = sizeof (send_addr);

    recv_file_dir (sock, send_buffer, recv_buffer, 
        send_addr, send_addr_len, subdir, filename);
    
    mkdir(subdir.c_str(), 0777);
    if (chdir(subdir.c_str()) < 0) {
        perror ("opening directory");
        exit (1);
    }

    ofstream ofs;
    ofs.open(filename.c_str());

    recv_file_content(sock, send_buffer, recv_buffer, 
        send_addr, send_addr_len, ofs);

    ofs.close();

    return 0;
}