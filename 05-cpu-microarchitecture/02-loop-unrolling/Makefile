CC := gcc
CFLAGS := -O3 -march=native -Wall -Wextra -std=c11 -fno-tree-vectorize
LDFLAGS :=

BIN_DIR := artifacts/bin
DATA_DIR := artifacts/data
SRC := src/loop_unrolling.c
OUT := $(BIN_DIR)/loop_unrolling

.PHONY: all clean dirs

all: dirs $(OUT)

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(DATA_DIR)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR) $(DATA_DIR)
