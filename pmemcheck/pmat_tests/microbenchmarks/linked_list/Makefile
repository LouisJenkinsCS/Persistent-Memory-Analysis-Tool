CC=gcc
CFLAGS=-fopenmp -lqsbr -ggdb3 -O0 -std=gnu11 -I/usr/local/include/ -L/usr/local/lib #-fsanitize=address
SRC=pmreorder_list.c
OBJ=$(SRC:.c=.o)
EXEC=pmreorder_list

all: $(EXEC)

$(EXEC):$(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(EXEC) $(OBJ) linked_list.bin
