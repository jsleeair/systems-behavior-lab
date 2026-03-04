CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11 -march=native
LDFLAGS :=

ROOT := .
SRC_DIR := $(ROOT)/src
ART_DIR := $(ROOT)/artifacts
BIN_DIR := $(ART_DIR)/bin

TARGET := $(BIN_DIR)/tlb_effect

SRCS := $(SRC_DIR)/tlb_effect.c $(SRC_DIR)/util.c
DEPS := $(SRCS:.c=.d)

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(ART_DIR)/out

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -MMD -MP -o $@ $(SRCS) $(LDFLAGS)

-include $(DEPS)

clean:
	rm -rf $(ART_DIR) $(SRC_DIR)/*.d
