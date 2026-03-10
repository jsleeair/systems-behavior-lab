CC := gcc
CFLAGS := -O2 -g -Wall -Wextra -Wpedantic -std=c11
LDFLAGS :=

SRC := src/context_switch.c
BIN := artifacts/bin/context_switch

.PHONY: all clean dirs run

all: dirs $(BIN)

dirs:
	mkdir -p artifacts/bin artifacts/data artifacts/plots

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data artifacts/plots
