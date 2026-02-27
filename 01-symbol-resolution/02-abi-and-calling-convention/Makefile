CC	:= gcc
CFLAGS	:= -Wall -Wextra -Wpedantic -std=c11
LDFLAGS	:=

# Two builds:
# - dbg: easier assembly (no inlining assumptions), keep frame pointer
# - opt: optimized (watch ABI-related transforms)
DBG_CFLAGS := -O0 -g3 -fno-omit-frame-pointer
OPT_CFLAGS := -O2 -g -fno-omit-frame-pointer

SRC_DIR := src
ART_DIR := artifacts
BIN_DIR := $(ART_DIR)/bin

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/abi_demo.c \
        $(SRC_DIR)/probe_sysv_amd64.S $(SRC_DIR)/variadic_wrapper_sysv_amd64.S
HDRS := $(SRC_DIR)/abi_demo.h

.PHONY: all clean dbg opt artifacts

all: dbg opt

dbg: $(BIN_DIR)/abi_dbg
opt: $(BIN_DIR)/abi_opt

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BIN_DIR)/abi_dbg: $(SRCS) $(HDRS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(DBG_CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
$(BIN_DIR)/abi_opt: $(SRCS) $(HDRS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OPT_CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

artifacts: all
	@mkdir -p $(ART_DIR)/asm $(ART_DIR)/objdump $(ART_DIR)/readelf $(ART_DIR)/nm

	# Produce assembly listings for main translation unit (debug and opt flags)
	$(CC) $(CFLAGS) $(DBG_CFLAGS) -S -o $(ART_DIR)/asm/main_dbg.s $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) $(OPT_CFLAGS) -S -o $(ART_DIR)/asm/main_opt.s $(SRC_DIR)/main.c

	# Full binary disassembly (with source interleave if debug info available)
	objdump -drwC $(BIN_DIR)/abi_dbg > $(ART_DIR)/objdump/abi_dbg.objdump.txt
	objdump -drwC $(BIN_DIR)/abi_opt > $(ART_DIR)/objdump/abi_opt.objdump.txt

	# Symbol tables, relocations, sections
	readelf -hSW $(BIN_DIR)/abi_dbg > $(ART_DIR)/readelf/abi_dbg.elf.txt
	readelf -hSW $(BIN_DIR)/abi_opt > $(ART_DIR)/readelf/abi_opt.elf.txt
	readelf -sW  $(BIN_DIR)/abi_dbg > $(ART_DIR)/readelf/abi_dbg.sym.txt
	readelf -sW  $(BIN_DIR)/abi_opt > $(ART_DIR)/readelf/abi_opt.sym.txt

	# nm view
	nm -n $(BIN_DIR)/abi_dbg > $(ART_DIR)/nm/abi_dbg.nm.txt
	nm -n $(BIN_DIR)/abi_opt > $(ART_DIR)/nm/abi_opt.nm.txt

clean:
	rm -rf $(ART_DIR)
