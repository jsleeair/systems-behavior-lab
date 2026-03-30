# Build rules for branch predictio lab.
#
# Important flags:
#   -O3			: optimize the benchmark normally
#   -fno-tree-vectorize : avoid vectorizing the loop
#   -fno-if-conversion	: discourage conversion of branches into cmov/select
#   -fno-if-conversion2 : same idea for later passes
#
# These flags help preserve the branchy nature of the test.

CC := gcc

CFLAGS := -O3 -std=c11 -Wall -Wextra -Wpedantic \
	  -fno-tree-vectorize \
	  -fno-if-conversion \
	  -fno-if-conversion2

LDFLAGS :=

SRC_DIR := src
BIN_DIR := artifacts/bin
DATA_DIR := artifacts/data

TARGET := $(BIN_DIR)/branch_prediction
SRCS := $(SRC_DIR)/branch_prediction.c

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(BIN_DIR) $(DATA_DIR)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR) $(DATA_DIR)
