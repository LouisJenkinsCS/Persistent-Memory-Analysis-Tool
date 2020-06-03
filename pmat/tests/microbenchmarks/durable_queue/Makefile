CC=gcc
override CFLAGS += -fopenmp -ggdb3 -O3 -std=gnu11 -Wall -Wunknown-pragmas
SRC=durable_queue.c durable_queue_test.c hazard.c
OBJ=$(SRC:.c=.o)
EXEC=durable_queue durable_queue_ordered durable_queue_verifier durable_queue_ordered_verifier

all: durable_queue durable_queue_verifier durable_queue_ordered durable_queue_ordered_verifier

durable_queue_verifier:durable_queue_verifier.o durable_queue.o hazard.o
	$(CC) $(CFLAGS) -o durable_queue_verifier $^

durable_queue:durable_queue.o durable_queue_test.o hazard.o
	$(CC) $(CFLAGS) -o durable_queue $^

durable_queue_ordered:durable_queue_ordered_test.o durable_queue.o hazard.o
	$(CC) $(CFLAGS) -o durable_queue_ordered $^

durable_queue_ordered_verifier:durable_queue.o durable_queue_ordered_verifier.o hazard.o
	$(CC) $(CFLAGS) -o durable_queue_ordered_verifier $^

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(EXEC) $(OBJ) *.bin* *.stderr* *.dump* *.stdout*
