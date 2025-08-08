# Makefile for RDMA Echo Example

CC = gcc
CFLAGS = -g -Wall -Wextra
LDFLAGS = -libverbs

# List of executables
TARGETS = server client

# List of object files
OBJS = rdma_common.o

.PHONY: all clean

all: $(TARGETS)

# Rule for the common object file
rdma_common.o: rdma_common.c rdma_common.h
	$(CC) $(CFLAGS) -c rdma_common.c -o rdma_common.o

# Rule for the server executable
server: server.c rdma_common.o
	$(CC) $(CFLAGS) -o server server.c rdma_common.o $(LDFLAGS)

# Rule for the client executable
client: client.c rdma_common.o
	$(CC) $(CFLAGS) -o client client.c rdma_common.o $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o
