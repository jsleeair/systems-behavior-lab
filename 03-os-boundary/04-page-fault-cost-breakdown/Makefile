CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11
LDFLAGS :=

SRC := src/page_fault_cost_breakdown.c
BIN := artifacts/bin/page_fault_cost_breakdown

.PHONY: all clean run dirs

all: $(BIN)

dirs:
	mkdir -p artifacts/bin
	mkdir -p artifacts/data

$(BIN): $(SRC) | dirs
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./run.sh

clean:
	rm -rf artifacts/bin/page_fault_cost_breakdown
	rm -rf artifacts/data/page_fault_cost_breakdown.csv
	rm -rf artifacts/data/page_fault_backing.bin
