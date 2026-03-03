# 01-false-sharing/Makefile

CC      := gcc
CFLAGS  := -O2 -g -Wall -Wextra -Wpedantic -pthread
LDFLAGS := -pthread

BIN_DIR := artifacts/bin
SRC_DIR := src

TARGET  := $(BIN_DIR)/false_sharing
SRCS    := $(SRC_DIR)/false_sharing.c

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BIN_DIR) artifacts/results

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/results
