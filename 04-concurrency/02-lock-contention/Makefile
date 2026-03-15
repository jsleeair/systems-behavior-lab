CC := gcc
CFLAGS := -O2 -g -Wall -Wpedantic -pthread
LDFLAGS := -pthread

SRC := src/main.c
OUT := artifacts/bin/lock_contention

.PHONY: all clean dirs run

all: dirs $(OUT)

dirs:
	mkdir -p artifacts/bin artifacts/data

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data
