CC          = g++
LD          = g++
CFLAGS      = -Wall -g

LDFLAGS     = -O2
STDFLAGS    = --std=c++11
DEFS        =

all:    sendfile recvfile

sendfile: sendfile.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o sendfile sendfile.cpp $(LDFLAGS) $(STDFLAGS)

recvfile: recvfile.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o recvfile recvfile.cpp $(LDFLAGS) $(STDFLAGS)

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*.*
