CC=gcc
override CFLAGS += -ggdb3 -O3 -std=gnu11
TEST_SRC=linked_list.c linked_list_test.c
TEST_OBJ=$(TEST_SRC:.c=.o)
TEST_EXEC=linked_list
VERF_SRC=linked_list.c linked_list_verifier.c
VERF_OBJ=$(VERF_SRC:.c=.o)
VERF_EXEC=linked_list_verifier

all: $(VERF_EXEC) $(TEST_EXEC)

$(VERF_EXEC):$(VERF_OBJ)
	$(CC) $(CFLAGS) -o $(VERF_EXEC) $(VERF_OBJ) $(LDFLAGS)

$(TEST_EXEC):$(TEST_OBJ)
	$(CC) $(CFLAGS) -o $(TEST_EXEC) $(TEST_OBJ) $(LDFLAGS)

%.o:%.c
	$(CC) $(CFLAGS) -c $< $(LDFLAGS) -o $@

clean:
	rm -rf $(VERF_EXEC) $(TEST_EXEC) $(VERF_OBJ) $(TEST_OBJ) *.bin* *.dump* *.stderr* *.stdout*
