CC=gcc
CFLAGS=-fopenmp -lqsbr -ggdb3 -O3 -std=gnu11 -I/usr/local/include/ -L/usr/local/lib -fsanitize=address
SRC=durable_queue.c durable_queue_test.c ../../deps/libqsbr/src/gc.c ../../deps/libqsbr/src/ebr.c ../../deps/libqsbr/src/qsbr.c
OBJ=$(SRC:.c=.o)
EXEC=durable_queue

all: $(EXEC)

$(EXEC):$(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(EXEC) $(OBJ)
