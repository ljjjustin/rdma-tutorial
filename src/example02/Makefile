CC=gcc
CFLAGS=-Wall -Werror -O2
INCLUDES=
LDFLAGS=
LIBS=-libverbs

SRCS=main.c
OBJS=$(SRCS:.c=.o)
PROG=main

.PHONY: all debug clean

all: $(PROG)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

debug: CFLAGS=-Wall -Werror -g -DDEBUG
debug: $(PROG)

clean:
	rm -f *.o *~ $(PROG)
