CC := gcc
CFLAGS := -O2 -march=native -Wall -Wextra -std=c11 -pthread
LDFLAGS := -pthread

SRC := src/ring_buffer.c
BIN := artifacts/bin/ring_buffer

.PHONY: all clean dirs

all: dirs $(BIN)

dirs:
	mkdir -p artifacts/bin
	mkdir -p artifacts/data

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -rf artifacts/bin artifacts/data
