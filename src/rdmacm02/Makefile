CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lrdmacm -libverbs

all: server client

server: server.c common.c common.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: client.c common.c common.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f server client
