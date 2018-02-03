#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <time.h>
#include <sys/stat.h>

/**************************************************/
/* a few simple linked list functions             */
/**************************************************/

const int PING_PONG_HEADER_SIZE = 10; // data part is excluded
const char* HTTP_VERSION = "HTTP/1.1";
#define BUFFER_LEN 700000LL
#define MAX_REQUEST_TOKEN_NUM 10
/* A linked list node data structure to maintain application
   information related to a connected socket */
struct node {
  int socket;
  struct sockaddr_in client_addr;
  char send_msg[BUFFER_LEN];
  char recv_msg[BUFFER_LEN];
  int remain_send_byte;
  int remain_recv_byte;
  char *next_send_pos;
  char *next_recv_pos;
  //int pending_data; /* flag to indicate whether there is more data to send */
  /* you will need to introduce some variables here to record
     all the information regarding this socket.
     e.g. what data needs to be sent next */
  struct node *next;
};

/* print message with mode <= DEBUGMODE
 */
void debug_print(int mode, const char *msg) {
#ifdef DEBUGMODE
  if (mode <= DEBUGMODE) {
    printf(msg);
  }
#endif
}


/* remove the data structure associated with a connected socket
   used when tearing down the connection */
void dump(struct node *head, int socket) {
  struct node *current, *temp;

  current = head;

  while (current->next) {
    if (current->next->socket == socket) {
      /* remove */
      temp = current->next;
      current->next = temp->next;
      free(temp); /* don't forget to free memory */
      return;
    } else {
      current = current->next;
    }
  }
}

/* create the data structure associated with a connected socket */
void add_ping_pong(struct node *head, int socket, struct sockaddr_in addr) {
  struct node *new_node;

  new_node = (struct node *)malloc(sizeof(struct node));
  if (new_node == NULL) {
    perror("allocate new_node");
    abort();
  }
  new_node->socket = socket;
  new_node->client_addr = addr;
  //new_node->pending_data = 0;
  new_node->remain_send_byte = 0;
  new_node->remain_recv_byte = PING_PONG_HEADER_SIZE;  // the header size
  new_node->next_send_pos = new_node->send_msg;
  new_node->next_recv_pos = new_node->recv_msg;
  new_node->next = head->next;
  head->next = new_node;
}

void send_remain_msg_ping_pong(struct node *current) {
              int count;
	      /* the socket is now ready to take more data */
	      /* the socket data structure should have information
                 describing what data is supposed to be sent next.
	         but here for simplicity, let's say we are just
                 sending whatever is in the buffer buf
               */
	      count = send(current->socket, 
                           current->next_send_pos, 
                           current->remain_send_byte, 
                           MSG_DONTWAIT);
	      if (count < 0) {
		if (errno == EAGAIN) {
		  /* we are trying to dump too much data down the socket,
		     it cannot take more for the time being 
		     will have to go back to select and wait til select
		     tells us the socket is ready for writing
		  */
                  // keeps the remain_send_byte non-zero
		} else {
		  /* something else is wrong */
                  perror("error during send remaining message");
                  abort();
		}
	      } else if (count < current->remain_send_byte) {
                current->next_send_pos += count;
                current->remain_send_byte -= count; 
              } else {
                // sending finished
                if (count != current->remain_send_byte) // this should not happen
                  perror("send more bytes than expected during sending remaining message");
                current->next_send_pos = current->send_msg;
                current->remain_send_byte = 0;
                current->next_recv_pos = current->recv_msg;
                current->remain_recv_byte = PING_PONG_HEADER_SIZE;
              }
	      /* note that it is important to check count for exactly
                 how many bytes were actually sent even when there are
                 no error. send() may send only a portion of the buffer
                 to be sent.
	      */
}

void print_ping_msg(char *recv_msg) {
  unsigned short size = ntohs(*(unsigned short*)recv_msg);

  char buffer[BUFFER_LEN], str_buffer[BUFFER_LEN];
  struct timeval tv;
  tv.tv_sec = ntohl(*(unsigned int*)(recv_msg+2));
  tv.tv_usec = ntohl(*(unsigned int*)(recv_msg+6));
 
  sprintf(str_buffer, "tv_sec: %d\n", (int)tv.tv_sec);
  debug_print(1, str_buffer);
  debug_print(2, "localtime\n");
  struct tm* tmp = localtime((time_t*)&(tv.tv_sec));
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


/* simple server, takes one parameter, the server port number */
int run_ping_pong(char **argv) {

  /* socket and option variables */
  int sock, new_sock, max;
  int optval = 1;

  /* server socket address variables */
  struct sockaddr_in sin, addr;
  unsigned short server_port = atoi(argv[1]);

  /* socket address variables for a connected client */
  socklen_t addr_len = sizeof(struct sockaddr_in);

  /* maximum number of pending connection requests */
  int BACKLOG = 100;

  /* variables for select */
  fd_set read_set, write_set;
  struct timeval time_out;
  int select_retval;

  /* number of bytes sent/received */
  int count;

  /* linked list for keeping track of connected sockets */
  struct node head;
  struct node *current, *next;

  char str_buffer[BUFFER_LEN];

  /* initialize dummy head node of linked list */
  head.socket = -1;
  head.next = 0;

  /* create a server socket to listen for TCP connection requests */
  if ((sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      perror ("opening TCP socket");
      abort ();
    }
  
  /* set option so we can reuse the port number quickly after a restart */
  if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof (optval)) <0)
    {
      perror ("setting TCP socket option");
      abort ();
    }

  /* fill in the address of the server socket */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons (server_port);
  
  /* bind server socket to the address */
  if (bind(sock, (struct sockaddr *) &sin, sizeof (sin)) < 0)
    {
      perror("binding socket to address");
      abort();
    }

  /* put the server socket in listen mode */
  if (listen (sock, BACKLOG) < 0)
    {
      perror ("listen on socket failed");
      abort();
    }

  /* now we keep waiting for incoming connections,
     check for incoming data to receive,
     check for ready socket to send more data */
  while (1)
    {

      /* set up the file descriptor bit map that select should be watching */
      FD_ZERO (&read_set); /* clear everything */
      FD_ZERO (&write_set); /* clear everything */

      FD_SET (sock, &read_set); /* put the listening socket in */
      max = sock; /* initialize max */

      /* put connected sockets into the read and write sets to monitor them */
      for (current = head.next; current; current = current->next) {
	FD_SET(current->socket, &read_set);

	if (current->remain_send_byte) {
	  /* there is data pending to be sent, monitor the socket
             in the write set so we know when it is ready to take more
             data */
	  FD_SET(current->socket, &write_set);
	}

	if (current->socket > max) {
	  /* update max if necessary */
	  max = current->socket;
	}
      }

      time_out.tv_usec = 100000; /* 1-tenth of a second timeout */
      time_out.tv_sec = 0;

      /* invoke select, make sure to pass max+1 !!! */
      select_retval = select(max+1, &read_set, &write_set, NULL, &time_out);
      if (select_retval < 0)
	{
	  perror ("select failed");
	  abort ();
	}

      if (select_retval == 0)
	{
	  /* no descriptor ready, timeout happened */
	  continue;
	}
      
      if (select_retval > 0) /* at least one file descriptor is ready */
	{
	  if (FD_ISSET(sock, &read_set)) /* check the server socket */
	    {
	      /* there is an incoming connection, try to accept it */
	      new_sock = accept (sock, (struct sockaddr *) &addr, &addr_len);
	      
	      if (new_sock < 0)
		{
		  perror ("error accepting connection");
		  abort ();
		}

	      /* make the socket non-blocking so send and recv will
                 return immediately if the socket is not ready.
                 this is important to ensure the server does not get
                 stuck when trying to send data to a socket that
                 has too much data to send already.
               */
	      if (fcntl (new_sock, F_SETFL, O_NONBLOCK) < 0)
		{
		  perror ("making socket non-blocking");
		  abort ();
		}
  
	      /* the connection is made, everything is ready */
	      /* let's see who's connecting to us */
	      sprintf(str_buffer, 
                      "Accepted connection. Client IP address is: %s\n",
		      inet_ntoa(addr.sin_addr));
              debug_print(1, str_buffer);

	      /* remember this client connection in our linked list */
	      add_ping_pong(&head, new_sock, addr);
	    }

	  /* check other connected sockets, see if there is
             anything to read or some socket is ready to send
             more pending data */
	  for (current = head.next; current; current = next) {
	    next = current->next;

	    /* see if we can now do some previously unsuccessful writes */
	    if (FD_ISSET(current->socket, &write_set)) {
              send_remain_msg_ping_pong(current);
	    }

	    if (FD_ISSET(current->socket, &read_set)) {
              sprintf(str_buffer, 
                      "before receive, remain_recv_byte: %d\n", 
                      current->remain_recv_byte);
              debug_print(1, str_buffer);
	      /* we have data from a client */
	      count = recv(current->socket, 
                           current->next_recv_pos, 
                           current->remain_recv_byte, 0);
              sprintf(str_buffer, "receive %d bytes\n", count);
              debug_print(1, str_buffer);
	      if (count <= 0) {
		/* something is wrong */
		if (count == 0) {
		  sprintf(str_buffer,
                          "Client closed connection. Client IP address is: %s\n", 
                          inet_ntoa(current->client_addr.sin_addr));
                  debug_print(1, str_buffer);
		} else {
		  perror("error receiving from a client");
		}

		/* connection is closed, clean up */
		close(current->socket);
		dump(&head, current->socket);
                continue;
	      } else if (count < current->remain_recv_byte) {
                current->next_recv_pos += count;
                current->remain_recv_byte -= count;
              } else {
	        if (current->next_recv_pos - current->recv_msg < PING_PONG_HEADER_SIZE) // still reading header
                {
                  debug_print(2, "finish receiving header\n");
                  if (count != current->remain_recv_byte) {  //should never happen
                    perror("receive more bytes than expected");
                  }
                  current->remain_recv_byte = (int)ntohs(*(unsigned short*)current->recv_msg);
                  current->remain_recv_byte = current->remain_recv_byte - PING_PONG_HEADER_SIZE;
                  if (current->remain_recv_byte == 0) {
                    debug_print(1, "no data to receive");
#ifdef DEBUGMODE
                    print_ping_msg(current->recv_msg);
#endif
                    // send pong message
                    current->remain_send_byte = (int)ntohs(*(unsigned short*)current->recv_msg);
                    memcpy(current->send_msg, current->recv_msg, current->remain_send_byte);
                    current->next_send_pos = current->send_msg;
                    current->remain_recv_byte = 0;
                    current->next_recv_pos = current->recv_msg;
                    send_remain_msg_ping_pong(current);
                  } else {
                    sprintf(str_buffer, "remain receive byte: %d\n", current->remain_recv_byte);
                    debug_print(1, str_buffer);
                    current->next_recv_pos += count;
                  }
                } else {
                  debug_print(2, "finish receive data\n");
                  if (count != current->remain_recv_byte) {  //should never happen
                    perror("receive more bytes than expected");
                  }
#ifdef DEBUGMODE
                  print_ping_msg(current->recv_msg);
#endif
                  // send pong message
                  current->remain_send_byte = (int)ntohs(*(unsigned short*)current->recv_msg);
                  memcpy(current->send_msg, current->recv_msg, current->remain_send_byte);
                  current->next_send_pos = current->send_msg;
                  current->remain_recv_byte = 0;
                  current->next_recv_pos = current->recv_msg;
                  send_remain_msg_ping_pong(current);
                }

		/* we got count bytes of data from the client */
                /* in general, the amount of data received in a recv()
                   call may not be a complete application message. it
                   is important to check the data received against
                   the message format you expect. if only a part of a
                   message has been received, you must wait and
                   receive the rest later when more data is available
                   to be read */
	      }
	    }
	  }
	}
    }
}

/* create the data structure associated with a connected socket */
void add_www(struct node *head, int socket, struct sockaddr_in addr) {
  struct node *new_node;

  new_node = (struct node *)malloc(sizeof(struct node));
  if (new_node == NULL) {
    perror("allocate new_node");
    abort();
  }
  new_node->socket = socket;
  new_node->client_addr = addr;
  //new_node->pending_data = 0;
  new_node->remain_send_byte = 0;
  new_node->remain_recv_byte = BUFFER_LEN;  // the header size
  new_node->next_send_pos = new_node->send_msg;
  new_node->next_recv_pos = new_node->recv_msg;
  new_node->next = head->next;
  head->next = new_node;
}

/*
 * if sending is finished which means the socket should be closed,
 * then return 1
 * else return 0
 */
int send_remain_msg_www(struct node *current) {
              int count;
	      /* the socket is now ready to take more data */
	      /* the socket data structure should have information
                 describing what data is supposed to be sent next.
	         but here for simplicity, let's say we are just
                 sending whatever is in the buffer buf
               */
	      count = send(current->socket, 
                           current->next_send_pos, 
                           current->remain_send_byte, 
                           MSG_DONTWAIT);
	      if (count < 0) {
		if (errno == EAGAIN) {
		  /* we are trying to dump too much data down the socket,
		     it cannot take more for the time being 
		     will have to go back to select and wait til select
		     tells us the socket is ready for writing
		  */
                  // keeps the remain_send_byte non-zero
		} else {
		  /* something else is wrong */
                  perror("error during send remaining message");
                  abort();
		}
	      } else if (count < current->remain_send_byte) {
                current->next_send_pos += count;
                current->remain_send_byte -= count; 
              } else {
                // sending finished
                if (count != current->remain_send_byte) // this should not happen
                  perror("send more bytes than expected during sending remaining message");
                return 1;
              }
	      /* note that it is important to check count for exactly
                 how many bytes were actually sent even when there are
                 no error. send() may send only a portion of the buffer
                 to be sent.
	      */
              return 0;
}


void parse_request(char *msg, char**result) {
  char *token = strtok(msg, " ");
  int count = 0;
  while (token != NULL && count < MAX_REQUEST_TOKEN_NUM) {
    result[count++] = token;
    token = strtok(NULL, " ");
  }
  int i;
  debug_print(1, "request tokens: \n");
  for (i = 0; i < count; i++) {
    debug_print(1, result[i]);
    debug_print(1, "\n");
  }
}

int run_www_server(char **argv) {

  /* socket and option variables */
  int sock, new_sock, max;
  int optval = 1;

  /* server socket address variables */
  struct sockaddr_in sin, addr;
  unsigned short server_port = atoi(argv[1]);
  char* root_directory = argv[3];

  /* socket address variables for a connected client */
  socklen_t addr_len = sizeof(struct sockaddr_in);

  /* maximum number of pending connection requests */
  int BACKLOG = 100;

  /* variables for select */
  fd_set read_set, write_set;
  struct timeval time_out;
  int select_retval;

  /* number of bytes sent/received */
  int count;

  /* linked list for keeping track of connected sockets */
  struct node head;
  struct node *current, *next;

  char str_buffer[BUFFER_LEN];

  /* initialize dummy head node of linked list */
  head.socket = -1;
  head.next = 0;

  /* create a server socket to listen for TCP connection requests */
  if ((sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      perror ("opening TCP socket");
      abort ();
    }
  
  /* set option so we can reuse the port number quickly after a restart */
  if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof (optval)) <0)
    {
      perror ("setting TCP socket option");
      abort ();
    }

  /* fill in the address of the server socket */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons (server_port);
  
  /* bind server socket to the address */
  if (bind(sock, (struct sockaddr *) &sin, sizeof (sin)) < 0)
    {
      perror("binding socket to address");
      abort();
    }

  /* put the server socket in listen mode */
  if (listen (sock, BACKLOG) < 0)
    {
      perror ("listen on socket failed");
      abort();
    }

  /* now we keep waiting for incoming connections,
     check for incoming data to receive,
     check for ready socket to send more data */
  while (1)
    {

      /* set up the file descriptor bit map that select should be watching */
      FD_ZERO (&read_set); /* clear everything */
      FD_ZERO (&write_set); /* clear everything */

      FD_SET (sock, &read_set); /* put the listening socket in */
      max = sock; /* initialize max */

      /* put connected sockets into the read and write sets to monitor them */
      for (current = head.next; current; current = current->next) {
	FD_SET(current->socket, &read_set);

	if (current->remain_send_byte) {
	  /* there is data pending to be sent, monitor the socket
             in the write set so we know when it is ready to take more
             data */
	  FD_SET(current->socket, &write_set);
	}

	if (current->socket > max) {
	  /* update max if necessary */
	  max = current->socket;
	}
      }

      time_out.tv_usec = 100000; /* 1-tenth of a second timeout */
      time_out.tv_sec = 0;

      /* invoke select, make sure to pass max+1 !!! */
      select_retval = select(max+1, &read_set, &write_set, NULL, &time_out);
      if (select_retval < 0)
	{
	  perror ("select failed");
	  abort ();
	}

      if (select_retval == 0)
	{
	  /* no descriptor ready, timeout happened */
	  continue;
	}
      
      if (select_retval > 0) /* at least one file descriptor is ready */
	{
	  if (FD_ISSET(sock, &read_set)) /* check the server socket */
	    {
	      /* there is an incoming connection, try to accept it */
	      new_sock = accept (sock, (struct sockaddr *) &addr, &addr_len);
	      
	      if (new_sock < 0)
		{
		  perror ("error accepting connection");
		  abort ();
		}

	      /* make the socket non-blocking so send and recv will
                 return immediately if the socket is not ready.
                 this is important to ensure the server does not get
                 stuck when trying to send data to a socket that
                 has too much data to send already.
               */
	      if (fcntl (new_sock, F_SETFL, O_NONBLOCK) < 0)
		{
		  perror ("making socket non-blocking");
		  abort ();
		}
  
	      /* the connection is made, everything is ready */
	      /* let's see who's connecting to us */
	      sprintf(str_buffer, 
                      "Accepted connection. Client IP address is: %s\n",
		      inet_ntoa(addr.sin_addr));
              debug_print(1, str_buffer);

	      /* remember this client connection in our linked list */
	      add_www(&head, new_sock, addr);
	    }

	  /* check other connected sockets, see if there is
             anything to read or some socket is ready to send
             more pending data */
	  for (current = head.next; current; current = next) {
	    next = current->next;

	    /* see if we can now do some previously unsuccessful writes */
	    if (FD_ISSET(current->socket, &write_set)) {
              if (send_remain_msg_www(current) == 1) {
                close(current->socket);
                dump(&head, current->socket);
                continue;
              }
	    }

	    if (FD_ISSET(current->socket, &read_set)) {
              sprintf(str_buffer, 
                      "before receive, remain_recv_byte: %d\n", 
                      current->remain_recv_byte);
              debug_print(1, str_buffer);
	      /* we have data from a client */
	      count = recv(current->socket, 
                           current->next_recv_pos, 
                           current->remain_recv_byte, 0);
              sprintf(str_buffer, "receive %d bytes\n", count);
              debug_print(1, str_buffer);
	      if (count <= 0) {
		/* something is wrong */
		if (count == 0) {
		  sprintf(str_buffer,
                          "Client closed connection. Client IP address is: %s\n", 
                          inet_ntoa(current->client_addr.sin_addr));
                  debug_print(1, str_buffer);
		} else {
		  perror("error receiving from a client");
		}

		/* connection is closed, clean up */
		close(current->socket);
		dump(&head, current->socket);
                continue;
	      } else {
                current->recv_msg[count] = '\0';
                char* end_first_line;
                end_first_line = strstr(current->recv_msg, "\r\n");
                if (end_first_line != NULL) {
                  *end_first_line = '\0';  // discard everything after the first line
                  debug_print(2, "finish receiving a request\n");
                  sprintf(str_buffer, "received msg: %s", current->recv_msg);
                  debug_print(1, str_buffer);

                  char **result = malloc(MAX_REQUEST_TOKEN_NUM*sizeof(char*));
                  char *parsed_request = malloc(strlen(current->recv_msg)*sizeof(char)+1);
                  if (result == NULL || parsed_request == NULL) {
                    perror("allocate result, parsed_request");
                    abort();
                  }
                  strcpy(parsed_request, current->recv_msg);
                  parse_request(parsed_request, result);

                  debug_print(2, "start procressing request\n");
                  current->send_msg[0] = '\0';
                  strcpy(current->send_msg, HTTP_VERSION);
                  if (strcmp(result[0], "GET") != 0) {
                    printf("Non-supported request\n");
                    strcat(current->send_msg, " 501 Not Implemented\r\n\r\n");
                    current->remain_send_byte = strlen(current->send_msg);
                  } else {
                    if (strstr(result[1], "../") != NULL) {
                      printf("request contains \"..\"\n");
                      strcat(current->send_msg, " 400 Bad Request\r\n\r\n");
                      current->remain_send_byte = strlen(current->send_msg);
                    } else {
                      debug_print(2, "legal path, start checking file existence\n");
                      char path[BUFFER_LEN];
                      strcpy(path, root_directory);
                      strcat(path, result[1]);
                      struct stat path_stat;
                      int file_found = 1;
                      if (stat(path, &path_stat) == -1) {
                        perror("wrong path");
                        strcat(current->send_msg, " 404 Not Found\r\n\r\n");
                        current->remain_send_byte = strlen(current->send_msg);
                        file_found = 0;
                      } else if (S_ISDIR(path_stat.st_mode)) {
                        strcat(path, "/index.html");
                        if (stat(path, &path_stat) == -1) {
                          perror("no index.html in the path");
                          strcat(current->send_msg, " 404 Not Found\r\n\r\n");
                          file_found = 0;
                          current->remain_send_byte = strlen(current->send_msg);
                        }
                      }
                      if (file_found) {
                        debug_print(2, "file found, start reading file\n");
                        FILE *pFile = fopen(path, "rb");
                        if (pFile == NULL) {
                          perror("open file failed");
                          strcat(current->send_msg, " 500 Internal Server Error\r\n\r\n");
                          current->remain_send_byte = strlen(current->send_msg);
                        } else {
                          long lSize;
                          char file_buffer[BUFFER_LEN];
                          
                          fseek(pFile, 0, SEEK_END);
                          lSize = ftell(pFile);
                          rewind(pFile);
                          if (lSize > BUFFER_LEN) {
                            printf("not enough buffer to read the file\n");
                            strcat(current->send_msg, " 500 Internal Server Error\r\n\r\n");
                            current->remain_send_byte = strlen(current->send_msg);
                          } else if (fread(file_buffer, 1, lSize, pFile) != lSize) {
                            perror("reading error");
                            strcat(current->send_msg, " 500 Internal Server Error\r\n\r\n");
                            current->remain_send_byte = strlen(current->send_msg);
                          } else {
                            debug_print(2, "file reading succeed, start copying to buffer\n");
                            strcat(current->send_msg, " 200 OK \r\nContent-Type: type\r\n\r\n");
                            current->remain_send_byte = strlen(current->send_msg) + lSize;
                            memcpy(current->send_msg + strlen(current->send_msg), file_buffer, lSize);
                          }
                        }
                      }
                    }
                  }
                  current->next_send_pos = current->send_msg;
                  current->remain_recv_byte = 0;
                  current->next_recv_pos = current->recv_msg;

                  free(parsed_request);
                  free(result);

                  debug_print(2, "send the response\n");
                  if (send_remain_msg_www(current) == 1) {
                    close(current->socket);
                    dump(&head, current->socket);
                    continue; 
                  }

                } else {
                  current->next_recv_pos += count;
                  current->remain_recv_byte -= count;
                }
	      }
	    }
	  }
	}
    }
}

/*****************************************/
/* main program                          */
/*****************************************/

int main(int argc, char **argv) {
  if (argc < 2 || argc > 4) {
    printf("wrong argument number\n");
    abort();
  }
  if (argc == 2 || strcmp(argv[2], "www") != 0) {
    run_ping_pong(argv);
  } else {
    if (argc == 3) {
      printf("root_directory needed\n");
      abort();
    } else {
      run_www_server(argv);
    }
  }
  return 0;
}

