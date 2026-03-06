CC := gcc
CFLAGS := -O3 -std=c11 -Wall -Wextra -pedantic -march=native
LDFLAGS :=

ROOT := .
SRC_DIR := $(ROOT)/src
ARTIFACTS_DIR := $(ROOT)/artifacts
BIN_DIR := $(ARTIFACTS_DIR)/bin
OBJ_DIR := $(ARTIFACTS_DIR)/obj
DATA_DIR := $(ARTIFACTS_DIR)/data

TARGET := $(BIN_DIR)/prefetch_locality
SRCS := $(SRC_DIR)/main.c
OBJS := $(OBJ_DIR)/main.o

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR) $(OBJ_DIR) $(DATA_DIR)

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(ARTIFACTS_DIR)
