CC=gcc
CFLAGS=-fopenmp -lqsbr -ggdb3 -O3 -std=gnu11 -I/usr/include/qsbr/ -fsanitize=address
SRC=durable_queue.c durable_queue_test.c
OBJ=$(SRC:.c=.o)
EXEC=durable_queue

all: $(EXEC)

$(EXEC):$(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(EXEC) $(OBJ)
