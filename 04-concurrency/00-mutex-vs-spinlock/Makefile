CC := gcc
CFLAGS := -O2 -g -Wall -Wextra -pthread
LDFLAGS := -pthread

SRC := src/main.c
BIN := artifacts/bin/mutex_vs_spinlock

.PHONY: all clean dirs run
	
all: dirs $(BIN)

dirs:
	mkdir -p artifacts/bin artifacts/data

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data
