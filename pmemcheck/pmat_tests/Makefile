CC=gcc
CFLAGS=-fopenmp -ggdb3 -O3 -std=gnu11
CFILES:=$(shell ls | grep .c)
EXECS:=$(CFILES:%.c=%)
BINS:=$(CFILES:%.c=%.bin*)

all:$(EXECS)
	
$(EXECS):$(CFILES)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	-rm -f $(EXECS) $(BINS)