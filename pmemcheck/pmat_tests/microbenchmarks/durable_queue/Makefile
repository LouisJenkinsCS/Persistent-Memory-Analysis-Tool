CC=gcc
CFLAGS=-fopenmp -lqsbr -ggdb3 -O0 -std=gnu11 -I/usr/local/include/ -L/usr/local/lib #-fsanitize=address
SRC=durable_queue.c durable_queue_test.c
OBJ=$(SRC:.c=.o)
EXEC=durable_queue
VERIFIER=durable_queue_verifier

all: $(EXEC) $(VERIFIER)

$(VERIFIER):durable_queue_verifier.o
	$(CC) $(CFLAGS) -o $(VERIFIER) durable_queue_verifier.o durable_queue.o

$(EXEC):$(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(EXEC) $(OBJ) *.bin*
