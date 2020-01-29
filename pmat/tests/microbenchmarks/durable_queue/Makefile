CC=gcc
override CFLAGS += -fopenmp -ggdb3 -O3 -std=gnu11 -Wall -Wunknown-pragmas
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
	rm -rf durable_queue_verifier.o $(VERIFIER) $(EXEC) $(OBJ) *.bin* *.stderr* *.dump* *.stdout*
