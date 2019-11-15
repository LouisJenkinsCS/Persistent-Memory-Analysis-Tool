CC=gcc
CFLAGS=-fopenmp -ggdb3 -O3 -std=gnu11 -I/usr/include/qsbr/
CFILES:=$(shell ls | grep .c)	
SRCS = $(wildcard *.c)
PROGS = $(patsubst %.c,%,$(SRCS))
BINS = $(patsubst %.c,%.bin*,$(SRCS))

all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	-rm -f $(EXECS) $(PROGS) $(BINS) *.stderr *.stdout *.dump
