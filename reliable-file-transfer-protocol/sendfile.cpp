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

#define RECV_SIZE 12
#define SEND_SIZE 10024
#define RESEND_TIME_OUT 1 // in msec
#define WINDOW_SIZE 100
#define PACKET_SIZE 10000
#define RESEND_DISABLE_ROUND 8

using std::cout;
using std::endl;
using std::ifstream;
using std::string;
using std::map;
using std::set;
using std::pair;
using std::to_string;

inline unsigned int calc_time (struct timeval &begin, struct timeval &end) {
    return ( (end.tv_sec - begin.tv_sec) * 1000 + end.tv_usec / 1000 ) - begin.tv_usec / 1000;
}

unsigned int get_file_size (const char* filename) {
    struct stat statbuf;
    stat (filename, &statbuf); 
    return statbuf.st_size;
}

unsigned int pack_up (char *buffer, const string &data, const unsigned int serial) {
    memcpy (buffer + 12, data.c_str(), data.length());

    PackHeader header(0, serial, data.length() + 12);

    *(PackHeader *) (buffer) = header;

    ((PackHeader *) (buffer))->crc32_val = 
        (unsigned int) crc32 (buffer + 4, header.packet_size - 4);

    return header.packet_size;
}

void send_file_dir (const int sock, char *send_buffer, char *recv_buffer, 
    struct sockaddr_in &recv_addr, socklen_t &recv_addr_len, const string &entire_dir) {

    std::ios::sync_with_stdio(false);

    unsigned int packed_size = pack_up (send_buffer, entire_dir, 0);

    while (true) {
        while (sendto (sock, send_buffer, packed_size, 0, 
            (struct sockaddr *) &recv_addr, recv_addr_len) < 0);

        if (recvfrom (sock, recv_buffer, RECV_SIZE, 0, 
            (struct sockaddr *) &recv_addr, &recv_addr_len) >= 0) {

            PackHeader *recv_header = (PackHeader *) (recv_buffer);
            
            if (recv_header->crc32_val == crc32 (recv_buffer + 4, 8) && 
                recv_header->serial_id == 0)
                break;
        }
    }
}

void send_file_content (int sock, char *send_buffer, char *recv_buffer,
    struct sockaddr_in &recv_addr, socklen_t &recv_addr_len, ifstream &ifs, unsigned int file_size) {

    std::ios::sync_with_stdio(false);

    cout << "sending file of size " << file_size << " bytes..." << endl;

    unsigned int window_lo = 1, window_hi = WINDOW_SIZE;
    unsigned int serial_now = 1, serial_end = 4294967295;

    struct timeval begin, end;
    set <unsigned int> resend;
    map <unsigned int, pair <struct timeval, string> > cache;

    int resend_flag = 0;
    while (!ifs.eof () || !cache.empty ()) {

        // send packet when :: not end of file && inside window && no resend
        if (!ifs.eof () && serial_now <= window_hi && 
            (resend.empty () || resend_flag < RESEND_DISABLE_ROUND)) {

            resend_flag++;

            char buf[PACKET_SIZE];
            ifs.read (buf, PACKET_SIZE);
            string to_be_sent = string (buf, ifs.gcount());

            unsigned int length = to_be_sent.length();
            if (length > 0) {

                cout << "[send data] " << (serial_now - 1) * PACKET_SIZE;
                cout << " (" << length << ")" << endl;

                if (gettimeofday (&begin, NULL) < 0) {
                    perror ("getting time");
                    exit (1);
                }

                cache[serial_now] = make_pair (begin, to_be_sent);

                unsigned int packed_size = pack_up (send_buffer, to_be_sent, serial_now);
                
                sendto (sock, send_buffer, packed_size, 0, 
                    (struct sockaddr *) &recv_addr, recv_addr_len);

                serial_now++;
            }
        }

        // resend packet
        else if (!resend.empty ()) {

            auto resend_iter = resend.begin();
            resend.erase(resend_iter);
            unsigned int serial_resend = *resend_iter;

            auto cache_it = cache.find(serial_resend);

            cout << "[send data] " << (serial_resend - 1) * PACKET_SIZE;
            cout << " (" << cache_it->second.second.length() << ")" << endl;

            if (gettimeofday (&begin, NULL) < 0) {
                perror ("getting time");
                exit (1);
            }

            // reset time stamp
            cache_it->second.first = begin;

            unsigned int packed_size = pack_up (send_buffer, 
                cache_it->second.second, serial_resend);

            if (sendto (sock, send_buffer, packed_size, 0, 
                (struct sockaddr *) &recv_addr, recv_addr_len) < 0) {
                resend.insert(serial_resend);
            }
        }

        if (resend_flag >= RESEND_DISABLE_ROUND)
            resend_flag = 0;

        // receive acknowledgement
        if (recvfrom (sock, recv_buffer, RECV_SIZE, 0, 
            (struct sockaddr *) &recv_addr, &recv_addr_len) >= 0) {
            
            PackHeader *recv_header = (PackHeader *) (recv_buffer);

            // mark packets before acknowledgement as reveived
            if (recv_header->crc32_val == crc32 (recv_buffer + 4, 8)) {

                if (recv_header->serial_id >= window_lo) {
                    for (unsigned int id = window_lo; id <= recv_header->serial_id; id++) {
                        auto cache_it = cache.find (id);
                        if (cache_it != cache.end ())
                            cache.erase(cache_it);
                    }

                    auto resend_it = resend.begin();
                    while (!resend.empty() && (*resend_it) <= recv_header->serial_id)
                        resend.erase(resend_it++);

                    window_lo = recv_header->serial_id + 1;
                    window_hi = recv_header->serial_id + WINDOW_SIZE;
                }
            }
        }

        // detect timeout
        if (gettimeofday (&end, NULL) < 0) {
            perror ("getting time");
            exit (1);
        }

        for (unsigned int id = window_lo; id <= serial_now; id++) {
            auto cache_it = cache.find(id);
            
            if (cache_it == cache.end())
                continue;

            unsigned int msec_elapsed = calc_time(cache_it->second.first, end);
            if (msec_elapsed >= RESEND_TIME_OUT)
                resend.insert (id); // if exists insert anyway
        }
    }

    int timer = 0;
    string dummy = "";
    while (timer < 300) {
        unsigned int packed_size = pack_up (send_buffer, dummy, serial_end);
        
        if (sendto (sock, send_buffer, packed_size, 0, 
            (struct sockaddr *) &recv_addr, recv_addr_len) < 0)
            continue;

        timer++;

        if (recvfrom (sock, recv_buffer, RECV_SIZE, 0, 
            (struct sockaddr *) &recv_addr, &recv_addr_len) >= 0) {

            timer = 0;

            PackHeader *recv_header = (PackHeader *) (recv_buffer);

            if (recv_header->crc32_val == crc32 (recv_buffer + 4, 8) && 
                recv_header->serial_id == serial_end)
                break;
        }
    }

    // end of transmission
    cout << "[completed]" << endl;
}

int main(int argc, char** argv) {

    std::ios::sync_with_stdio(false);

    int sock;
    struct sockaddr_in recv_addr;
    socklen_t recv_addr_len = sizeof (struct sockaddr_in);

    if (argc != 5) {
        printf("wrong input format\n");
        printf("./sendfile -r <recv_host>:<recv_port> -f <subdir>/<filename>\n");
        exit(1);
    }

    unsigned int recv_s_addr = 0;
    unsigned short recv_port = 0;
    string subdir;
    string filename;

    for (int i = 1; i < 5; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            string receiver_str = argv[i + 1];
            size_t found = receiver_str.rfind (":");
            recv_s_addr = inet_addr (receiver_str.substr(0, found).c_str());
            recv_port = atoi (receiver_str.substr(found + 1).c_str());
            i++;
        }
        else if (strcmp(argv[i], "-f") == 0) {
            string file_str = argv[i + 1];
            size_t found = file_str.rfind ("/");
            subdir = file_str.substr (0, found);
            filename = file_str.substr (found + 1);
            i++;
        }
    }

    string entire_file_dir;

    char recv_buffer[RECV_SIZE], send_buffer[SEND_SIZE];
    memset(recv_buffer, 0, RECV_SIZE);
    memset(send_buffer, 0, SEND_SIZE);

    memset (&recv_addr, 0, sizeof (recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = recv_s_addr;
    recv_addr.sin_port = htons (recv_port);

    if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("opening UDP socket");
        exit (1);
    }

    if (fcntl (sock, F_SETFL, O_NONBLOCK) < 0) {
        perror ("making socket non-blocking");
        exit (1);
    }

    entire_file_dir = subdir;
    if (*subdir.end () != '/')
        entire_file_dir += '/';
    entire_file_dir += '\n';
    entire_file_dir += filename;

    if (chdir(subdir.c_str()) < 0) {
        perror ("opening directory");
        exit (1);
    }

    /********** measure time **********/
    struct timeval send_begin;
    gettimeofday (&send_begin, NULL);
    /**********************************/

    unsigned int file_size = get_file_size (filename.c_str());

    send_file_dir (sock, send_buffer, recv_buffer, 
        recv_addr, recv_addr_len, entire_file_dir);

    ifstream ifs;
    ifs.open (filename.c_str());

    send_file_content (sock, send_buffer, recv_buffer, 
        recv_addr, recv_addr_len, ifs, file_size);

    ifs.close();

    /********** measure time **********/
    struct timeval send_end;
    gettimeofday (&send_end, NULL);
    cout << "Total time elapsed: " << calc_time(send_begin, send_end) << " msec" << endl;
    /**********************************/

    return 0;
}
