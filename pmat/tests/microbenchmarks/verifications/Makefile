CC=gcc
override CFLAGS += -fopenmp -ggdb3 -O3 -std=gnu11 -Wall -Wunknown-pragmas
SRC=nop.c
OBJ=$(SRC:.c=.o)
EXEC=nop
VERIFIER=nop_verifier

all: $(EXEC) $(VERIFIER)

$(VERIFIER):nop_verifier.o
	$(CC) $(CFLAGS) -o $(VERIFIER) nop_verifier.o

$(EXEC):$(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf nop.o $(VERIFIER) $(EXEC) $(OBJ) *.bin* *.stderr* *.dump* *.stdout*

