CC := gcc
CFLAGS := -O2 -march=native -Wall -Wextra -std=c11
BIN_DIR := artifacts/bin
SRC_DIR := src

TARGET := $(BIN_DIR)/perf_stat_basics

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC_DIR)/perf_stat_basics.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf artifacts
