CC := gcc
CFLAGS := -O2 -g -Wall -Wextra -std=c11
LDFLAGS :=

SRC := src/cow.c
BIN := artifacts/bin/cow

.PHONY: all clean dirs

all: dirs $(BIN)

dirs:
	mkdir -p artifacts/bin artifacts/data

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -rf artifacts/bin/cow artifacts/data/*.csv
