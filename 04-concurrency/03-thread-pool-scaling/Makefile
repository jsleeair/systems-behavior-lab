CC := gcc
CFLAGS := -O2 -g -Wall -Wextra -pthread
LDFLAGS := -pthread

BIN_DIR := artifacts/bin
DATA_DIR := artifacts/data
SRC := src/thread_pool_scaling.c
TARGET := $(BIN_DIR)/thread_pool_scaling

.PHONY: all clean dirs run

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(DATA_DIR)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data
