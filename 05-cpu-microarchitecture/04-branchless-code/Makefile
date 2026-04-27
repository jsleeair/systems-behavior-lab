CC := gcc
CFLAGS := -O3 -march=native -Wall -Wextra -std=c11
LDFLAGS :=

BIN_DIR := artifacts/bin
DATA_DIR := artifacts/data
SRC_DIR := src

TARGET := $(BIN_DIR)/branchless_code
SRC := $(SRC_DIR)/branchless_code.c

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(DATA_DIR)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf artifacts
