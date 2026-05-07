CC := gcc

CFLAGS := -O2 -g -Wall -Wextra -std=c11 -march=native
CFLAGS += -fno-omit-frame-pointer
CFLAGS += -fno-optimize-sibling-calls

LDFLAGS :=

BIN_DIR := artifacts/bin
DATA_DIR := artifacts/data
REPORT_DIR := artifacts/reports

TARGET := $(BIN_DIR)/flamegraph_lab
SRC := src/flamegraph_lab.c

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR) $(DATA_DIR) $(REPORT_DIR)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf artifacts
