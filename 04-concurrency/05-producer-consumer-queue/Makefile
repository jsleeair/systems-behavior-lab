CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11 -pthread
LDFLAGS := -pthread

SRC := src/producer_consumer_queue.c
BIN := artifacts/bin/producer_consumer_queue

.PHONY: all clean dirs

all: dirs $(BIN)

dirs:
	mkdir -p artifacts/bin
	mkdir -p artifacts/data

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -rf artifacts/bin artifacts/data
