#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#define BUFFER_SIZE 70000
const int PING_PONG_HEADER_SIZE = 10;

void debug_print(int mode, char* msg) {
#ifdef DEBUGMODE
  if (mode <= DEBUGMODE) {
    printf(msg);
  }
#endif
}

void print_ping_msg(char *recv_msg) {
  unsigned short size = ntohs(*(unsigned short*)recv_msg);

  char buffer[BUFFER_SIZE], str_buffer[BUFFER_SIZE];
  struct timeval tv;
  tv.tv_sec = ntohl(*(unsigned int*)(recv_msg+2));
  tv.tv_usec = ntohl(*(unsigned int*)(recv_msg+6));
 
  sprintf(str_buffer, "tv_sec: %d\n", (int)tv.tv_sec);
  debug_print(1, str_buffer);
  debug_print(2, "localtime\n");
  struct tm* tmp = (struct tm*) localtime((time_t*)&(tv.tv_sec));
  if (tmp == NULL) {
    perror("localtime failed");
    abort();
  }
  debug_print(2, "format time\n");
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tmp);
  debug_print(2, "before print ping msg\n");
  sprintf(str_buffer, "size: %d\n", size);
  debug_print(1, str_buffer);
  sprintf(str_buffer, "timestamp: %s.%06d\n", buffer, (int)tv.tv_usec);
  debug_print(1, str_buffer);
  sprintf(str_buffer, "data: %s\n", recv_msg+PING_PONG_HEADER_SIZE);
  debug_print(3, str_buffer);
}


/* simple client, takes two parameters, the server domain name,
   and the server port number */

int main(int argc, char** argv) {

  if (argc != 5) {
    perror("./client hostname port size count\n");
    abort();
  }
  /* our client socket */
  int sock;

  /* address structure for identifying the server */
  struct sockaddr_in sin;

  /* convert server domain name to IP address */
  struct hostent *host = gethostbyname(argv[1]);
  unsigned int server_addr = *(unsigned int *) host->h_addr_list[0];

  /* server port number */
  unsigned short server_port = atoi (argv[2]);

  char str_buffer[BUFFER_SIZE];

  char *recvbuffer, *sendbuffer;
  char *buffer_pos;
  unsigned short size = atoi(argv[3]);
  int arg_count = atoi(argv[4]);
  long long total_latency;
  int count, remain;

  struct timeval tv, tv_in_ping;
  long long time_diff_usec;

  /* allocate a memory buffer in the heap */
  /* putting a buffer on the stack like:

         char buffer[500];

     leaves the potential for
     buffer overflow vulnerability */
  recvbuffer = (char *) malloc(BUFFER_SIZE);
  if (!recvbuffer)
    {
      perror("failed to allocated buffer");
      abort();
    }

  sendbuffer = (char *) malloc(BUFFER_SIZE);
  if (!sendbuffer)
    {
      perror("failed to allocated sendbuffer");
      abort();
    }


  /* create a socket */
  if ((sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      perror ("opening TCP socket");
      abort ();
    }

  /* fill in the server's address */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = server_addr;
  sin.sin_port = htons(server_port);

  /* connect to the server */
  if (connect(sock, (struct sockaddr *) &sin, sizeof (sin)) < 0)
    {
      perror("connect to server failed");
      abort();
    }

  // prepare data
  int i;
  *(unsigned short*)sendbuffer = htons(size);
  for (i = 0; i < size - PING_PONG_HEADER_SIZE; i++) {
    sendbuffer[i+PING_PONG_HEADER_SIZE] = '0'+(i%PING_PONG_HEADER_SIZE);
  }
  
  total_latency = 0;
  for (i = 0; i < arg_count; i++) {
    gettimeofday(&tv, NULL);
    buffer_pos = sendbuffer + 2;
    *(unsigned int*)buffer_pos = htonl(tv.tv_sec);
    buffer_pos = sendbuffer + 6;
    *(unsigned int*)buffer_pos = htonl(tv.tv_usec);
    sprintf(str_buffer, "before send %d\n", i);
    debug_print(2, str_buffer);
    count = send(sock, sendbuffer, size, 0);
    if (count <= 0)
    {
      perror("send failure");
      abort();
    }
    sprintf(str_buffer, "send %d bytes\n", count);
    debug_print(1, str_buffer);
    remain = size - count;
    while (remain != 0) {
      count = send(sock, sendbuffer+count, remain, 0);
      if (count <= 0)
      {
        perror("send failure");
        abort();
      }
      sprintf(str_buffer, "send %d bytes\n", count);
      debug_print(1, str_buffer);
      remain -= count;
    }

    /* everything looks good, since we are expecting a
       message from the server in this example, let's try receiving a
       message from the socket. this call will block until some data
       has been received */
    sprintf(str_buffer, "before receive %d\n", i);
    debug_print(2, str_buffer);
    count = recv(sock, recvbuffer, size, 0);
    if (count <= 0)
    {
      perror("receive failure");
      abort();
    }
    sprintf(str_buffer, "receive %d bytes\n", count);
    debug_print(1, str_buffer);
    remain = size - count;
    while (remain != 0) {
      count = recv(sock, recvbuffer+count, size, 0);
      if (count <= 0)
      {
        perror("receive failure");
        abort();
      }
      sprintf(str_buffer, "receive %d bytes\n", count);
      debug_print(1, str_buffer);
      remain -= count;
    }

    gettimeofday(&tv, NULL);

    tv_in_ping.tv_sec = ntohl(*(int*)(recvbuffer+2));
    tv_in_ping.tv_usec = ntohl(*(int*)(recvbuffer+6));
    sprintf(str_buffer, 
            "current tv_sec: %d, current tv_usec: %d\n", 
            (int)tv.tv_sec, (int)tv.tv_usec);
    debug_print(1, str_buffer);
    sprintf(str_buffer,
           "ping tv_sec:    %d, ping tv_usec:    %d\n", 
           (int)tv_in_ping.tv_sec, 
           (int)tv_in_ping.tv_usec);
    debug_print(1, str_buffer);
#ifdef DEBUGMODE
    //print_ping_msg(recvbuffer);
#endif
    time_diff_usec = (tv.tv_sec - ntohl(*(int*)(recvbuffer+2))) * 1000000LL
                   + (tv.tv_usec - ntohl(*(int*)(recvbuffer+6)));
    total_latency += time_diff_usec;
  }
  /* free the resources, generally important! */
  close(sock);
  free(recvbuffer);
  free(sendbuffer);

  printf("average latency: %.3lf msec\n", (double)total_latency/1000/arg_count);

  return 0;
}
