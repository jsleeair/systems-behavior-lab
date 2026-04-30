CC := gcc

CFLAGS := -O2 -march=native -Wall -Wextra -std=c11
CFLAGS += -fno-omit-frame-pointer
CFLAGS += -fno-optimize-sibling-calls
CFLAGS += -g

BIN_DIR := artifacts/bin
DATA_BIN := artifacts/data
PERF_DIR := artifacts/perf

TARGET := $(BIN_DIR)/perf_record_hotspot
SRC := src/perf_record_hotspot.c

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR) $(DATA_DIR) $(PERF_DIR)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf artifacts
