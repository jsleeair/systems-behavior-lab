# Makefile - PLT/GOT analysis lab (src/ + artifacts/ convention)
#
# Key properties:
#   - src/ contains source code only
#   - build/ contains build outputs (binary)
#   - artifacts/ contains collected evidence for analysis
#
# IMPORTANT:
#   "all" MUST depend on "dirs" and the final binary.
#   Otherwise 'make all' can become a no-op and run.sh will fail.

CC	:= gcc
CFLAGS	:= -O0 -g -fno-omit-frame-pointer -Wall -Wextra -Werror
LDFLAGS	:= -no-pie -fno-pie

SRC_DIR	:= src
BUILD_DIR := build
ART_DIR := artifacts

TARGET := $(BUILD_DIR)/main
SRC := $(SRC_DIR)/main.c

.PHONY: all clean dirs

# Always create directories and build the binary.
all: dirs $(TARGET)

# Create output directories used by scripts/tools.
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(ART_DIR)/static
	@mkdir -p $(ART_DIR)/runtime
	@mkdir -p $(ART_DIR)/disasm

# Build the executable.
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	@rm -rf $(BUILD_DIR) $(ART_DIR)
