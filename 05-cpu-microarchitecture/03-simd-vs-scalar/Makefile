CC := gcc
CFLAGS := -O3 -march=native -mavx2 -mfma -Wall -Wextra -std=c11
LDFLAGS :=

SRC := src/simd_vs_scalar.c
BIN := artifacts/bin/simd_vs_scalar

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p artifacts/bin
	mkdir -p artifacts/data
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -rf artifacts/bin artifacts/data

.PHONY: all clean
