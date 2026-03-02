# 02-static-vs-dynamic-link
#
# Goals:
#  - Build a small library as both:
#      * static archive (.a)
#      * shared object (.so)
#  - Build two executables:
#      * main_dynamic	: dynamically links against libgreet.so
#      * main_staticlib	: statically links libgreet.a (but still uses dynamic libc)
#  - Produce inspection logs under artifacts/logs

CC	:= gcc
CFLAGS	:= -O0 -g -Wall -Wextra -fno-omit-frame-pointer
LDFLAGS	:= 

ROOT	:= $(abspath .)
SRC_DIR	:= src
ART_DIR	:= artifacts
BIN_DIR	:= $(ART_DIR)/bin
LIB_DIR	:= $(ART_DIR)/lib
LOG_DIR	:= $(ART_DIR)/logs

# Sources
LIB_SRC	:= $(SRC_DIR)/greet.c
LIB_HDR	:= $(SRC_DIR)/greet.h
MAIN_SRC:= $(SRC_DIR)/main.c

# Objects
LIB_PIC_OBJ	:= $(LIB_DIR)/greet.pic.o
LIB_OBJ		:= $(LIB_DIR)/greet.o

# Outputs
SO	:= $(LIB_DIR)/libgreet.so
A	:= $(LIB_DIR)/libgreet.a

DYN_BIN		:= $(BIN_DIR)/main_dynamic
STATICL_BIN	:= $(BIN_DIR)/main_staticlib

.PHONY: all dirs clean run inspect

all: dirs $(SO) $(A) $(DYN_BIN) $(STATICL_BIN)

dirs:
	@mkdir -p $(BIN_DIR) $(LIB_DIR) $(LOG_DIR)

# ---- Library builds ----

# Position-independent object for shared library
$(LIB_PIC_OBJ): $(LIB_SRC) $(LIB_HDR) | dirs
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Shared library (.so)
$(SO): $(LIB_PIC_OBJ) | dirs
	$(CC) -shared -o $@ $<

# Normal object for static archive
$(LIB_OBJ): $(LIB_SRC) $(LIB_HDR) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Static library (.a)
$(A): $(LIB_OBJ) | dirs
	ar rcs $@ $<

# ---- Executable builds ----

# Dynamic link against libgreet.so
# We embed a RUNPATH ($ORIGIN/../lib) so the executable can find the .so
# relative to its own location (artifacts/bin -> artifacts/lib).
$(DYN_BIN): $(MAIN_SRC) $(LIB_HDR) $(SO) | dirs
	$(CC) $(CFLAGS) $(MAIN_SRC) -o $@ \
		-L$(LIB_DIR) -lgreet \
		-Wl,-rpath,'$$ORIGIN/../lib'

# Static link libgreet.a only (libc remains dynamic)
# Using the archive directly avoids relying on -Bstatic / -Bdynamic tricks.
$(STATICL_BIN): $(MAIN_SRC) $(LIB_HDR) $(A) | dirs
	$(CC) $(CFLAGS) $(MAIN_SRC) -o $@ $(A)

run: all
	@./run.sh

inspect: all
	@echo "Writing inspection logs to $(LOG_DIR)"
	@echo "== size ==" > $(LOG_DIR)/size.txt
	@size $(DYN_BIN) $(STATICL_BIN) >> $(LOG_DIR)/size.txt

	@echo "== ldd (dynamic) ==" > $(LOG_DIR)/ldd_dynamic.txt
	@ldd $(DYN_BIN) >> $(LOG_DIR)/ldd_dynamic.txt

	@echo "== ldd (staticlib) ==" > $(LOG_DIR)/ldd_staticlib.txt
	@ldd -d $(STATICL_BIN) >> $(LOG_DIR)/ldd_staticlib.txt

	@echo "== readelf -d (dynamic) ==" > $(LOG_DIR)/readelf_dynamic.txt
	@readelf -d $(DYN_BIN) >> $(LOG_DIR)/readelf_dynamic.txt

	@echo "== readelf -d (staticlib) ==" > $(LOG_DIR)/readelf_staticlib.txt
	@readelf -d $(STATICL_BIN) >> $(LOG_DIR)/readelf_staticlib.txt

	@echo "== nm (dynamic, grep greet) ==" > $(LOG_DIR)/nm_dynamic_greet.txt
	@nm -an $(DYN_BIN) | grep -E "greet|greet_counter" >> $(LOG_DIR)/nm_dynamic_greet.txt || true

	@echo "== nm (staticlib, grep greet) ==" > $(LOG_DIR)/nm_staticlib_greet.txt
	@nm -an $(STATICL_BIN) | grep -E "greet|greet_counter" >> $(LOG_DIR)/nm_staticlib_greet.txt || true
	@echo "Done."

clean:
	rm -rf $(ART_DIR)
