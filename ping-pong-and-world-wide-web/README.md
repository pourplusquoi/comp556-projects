#COMP 556 Project 1

##Makefile
Go to the directory where ``Makefile`` locates, use command ``make`` to compile ``server.c`` into ``server`` and ``client.c`` into ``client``.

##How to Test
###Client
To run client, use command ``./client hostname port size count``.
###Server
To run server on ping-pong mode, use command ``./server port``; and to run server on web server mode, use command ``./server port www root_directory``.

##The Ping-Pong Client and Server
In this part, we tested our client on clear server glass, and our server on clear server water. We sent messages of length in interval 10 <= size <= 65535, client and server worked fine. The latency of transmission is reasonable.

Besides, we tested ping-pong server by sending messages to it from multiple client at the same time. The results showed that ping-pong server can handle multiple concurrent connections.

##The Web Server
We saved several web pages to clear server, and used them as test files.

If the request is not ``GET``, our web server would return error code ``501 Not Implemented``. If the request contains ``../`` path elements, our web server would return error code ``400 Bad Request``. If the file or directory doesn't exist, or the directory exists but doen't contain a file named ``index.html``, our web server would return error code ``404 Not Found``.

If the file is found, but our server doesn't have the permission to read it, or the read buffer is full, then server would return error code ``500 Internal Server Error``; otherwise, the content in file would be sent back to client.

If the input is a directory, and the directory contains a file named ``index.html`` in it, but our server doesn't have the permission to read ``./index.html``, or the read buffer is full, then it would return error code ``500 Internal Server Error``; otherwise, the content in ``./index.html`` would be sent back to client.

Moreover, our web server also supports multiple concurrent connections.