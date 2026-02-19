# build-and-binary/00-translation-unit/Makefile
CC := gcc
CFLAGS_COMMON := -Wall -Wextra -g
LDLIBS :=

# options by step
CFLAGS_STEP1 := -O0
CFLAGS_STEP2 := -O0
CFLAGS_STEP3 := -O0
CFLAGS_STEP4 := -O0

STEPS := step1-multiple-definition step2-fix-header-declare step3-static-in-header step4-static-inline-in-header

.PHONY: all clean distclean run $(STEPS)

all: $(STEPS)

# ---- Generic build rules per step ----
# step1,3,4 have no add.c
step1-multiple-definition: step1-multiple-definition/app
step3-static-in-header: step3-static-in-header/app
step4-static-inline-in-header: step4-static-inline-in-header/app

# step2 has add.c
step2-fix-header-declare: step2-fix-header-declare/app

# Build commands
step1-multiple-definition/app: step1-multiple-definition/main.c step1-multiple-definition/a.c step1-multiple-definition/b.c step1-multiple-definition/add.h
	@$(MAKE) -s build_no_add DIR=step1-multiple-definition CFLAGS_STEP=$(CFLAGS_STEP1)

step3-static-in-header/app: step3-static-in-header/main.c step3-static-in-header/a.c step3-static-in-header/b.c step3-static-in-header/add.h
	@$(MAKE) -s build_no_add DIR=step3-static-in-header CFLAGS_STEP=$(CFLAGS_STEP3)

step4-static-inline-in-header/app: step4-static-inline-in-header/main.c step4-static-inline-in-header/a.c step4-static-inline-in-header/b.c step4-static-inline-in-header/add.h
	@$(MAKE) -s build_no_add DIR=step4-static-inline-in-header CFLAGS_STEP=$(CFLAGS_STEP4)

step2-fix-header-declare/app: step2-fix-header-declare/main.c step2-fix-header-declare/a.c step2-fix-header-declare/b.c step2-fix-header-declare/add.c step2-fix-header-declare/add.h
	@$(MAKE) -s build_with_add DIR=step2-fix-header-declare CFLAGS_STEP=$(CFLAGS_STEP2)

# Internal recipes (called via recursive make)
.PHONY: build_no_add build_with_add collect

build_no_add:
	@mkdir -p $(DIR)/artifacts
	@echo "[BUILD] $(DIR) (no add.c)"
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/main.c -o $(DIR)/artifacts/main.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/a.c    -o $(DIR)/artifacts/a.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/b.c    -o $(DIR)/artifacts/b.o
	@# step1 fails here. make should go on
	@set +e; \
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) \
		$(DIR)/artifacts/main.o $(DIR)/artifacts/a.o $(DIR)/artifacts/b.o \
		$(LDLIBS) -o $(DIR)/app 2> $(DIR)/artifacts/link.stderr ; \
	rc=$$?; \
	if [ $$rc -ne 0 ]; then \
		echo "[LINK] $(DIR) failed as expected (rc=$$rc)"; \
	else \
		echo "[LINK] $(DIR) succeeded"; \
	fi; \
	exit 0
	@$(MAKE) -s collect DIR=$(DIR)

build_with_add:
	@mkdir -p $(DIR)/artifacts
	@echo "[BUILD] $(DIR) (with add.c)"
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/main.c -o $(DIR)/artifacts/main.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/a.c    -o $(DIR)/artifacts/a.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/b.c    -o $(DIR)/artifacts/b.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) -c $(DIR)/add.c  -o $(DIR)/artifacts/add.o
	@$(CC) $(CFLAGS_COMMON) $(CFLAGS_STEP) \
		$(DIR)/artifacts/main.o $(DIR)/artifacts/a.o $(DIR)/artifacts/b.o $(DIR)/artifacts/add.o \
		$(LDLIBS) -o $(DIR)/app 2> $(DIR)/artifacts/link.stderr
	@$(MAKE) -s collect DIR=$(DIR)

collect:
	@echo "[COLLECT] $(DIR)"
	@mkdir -p $(DIR)/artifacts

	@# 1) preprocessing evidence
	@$(CC) -E $(DIR)/a.c > $(DIR)/artifacts/a.i
	@$(CC) -E $(DIR)/b.c > $(DIR)/artifacts/b.i

	@# 2) symbols
	@nm -C $(DIR)/artifacts/a.o    > $(DIR)/artifacts/nm_a_o.txt
	@nm -C $(DIR)/artifacts/b.o    > $(DIR)/artifacts/nm_b_o.txt
	@nm -C $(DIR)/artifacts/main.o > $(DIR)/artifacts/nm_main_o.txt
	@if [ -f "$(DIR)/artifacts/add.o" ]; then nm -C $(DIR)/artifacts/add.o > $(DIR)/artifacts/nm_add_o.txt; fi
	@if [ -f "$(DIR)/app" ]; then nm -C $(DIR)/app > $(DIR)/artifacts/nm_app.txt; fi

	@# 3) ELF symbol table / relocations (readelf)
	@readelf -h $(DIR)/artifacts/a.o > $(DIR)/artifacts/readelf_h_a_o.txt
	@readelf -s $(DIR)/artifacts/a.o > $(DIR)/artifacts/readelf_s_a_o.txt
	@readelf -r $(DIR)/artifacts/a.o > $(DIR)/artifacts/readelf_r_a_o.txt

	@# 4) disassembly (optional but useful)
	@objdump -d $(DIR)/artifacts/a.o > $(DIR)/artifacts/objdump_d_a_o.txt

	@# 5) runnable steps execute
	@if [ -f "$(DIR)/app" ]; then \
		./$(DIR)/app > $(DIR)/artifacts/run.stdout 2> $(DIR)/artifacts/run.stderr || true; \
	fi

run: all
	@bash ./run.sh

clean:
	@for d in $(STEPS); do \
		rm -rf $$d/artifacts $$d/app; \
	done

distclean: clean
	@echo "[OK] distclean"

