# 00-build-and-binary / 01-weak-vs-strong-symbol
#
# This Makefile builds mutiple "cases" to demonstrate how the linker resolves
# weak/strong symbols and how tentative definitions behave uner -fcommon/-fno-common.

CC	:= gcc
CFLAGS	:= -O0 -g -Wall -Wextra -std=c11
LDFLAGS	:=

OBJDIR	:= artifacts/obj
BINDIR	:= artifacts/bin
LOGDIR	:= artifacts/logs

NM	:= nm
READELF	:= readelf

.PHONY: all clean dirs \
	case_strong_strong \
	case_weak_strong_var \
	case_weak_strong_func \
	case_common_fcommon \
	case_common_fnocommon \
	inspect

all: dirs \
	case_weak_strong_var \
	case_weak_strong_func \
	case_common_fcommon

dirs:
	@mkdir -p $(OBJDIR) $(BINDIR) $(LOGDIR)

clean:
	@rm -rf artifacts

# -------------------------------------
# Case 1: Strong vs Strong -> link error
# -------------------------------------
case_strong_strong: dirs
	@echo "== Case: strong vs strong (expect link failure) =="
	@$(CC) $(CFLAGS) -c src/strong_strong_a.c -o $(OBJDIR)/strong_strong_a.o
	@$(CC) $(CFLAGS) -c src/strong_strong_b.c -o $(OBJDIR)/strong_strong_b.o
	@set -e; \
		( $(CC) $(LDFLAGS) $(OBJDIR)/strong_strong_a.o $(OBJDIR)/strong_strong_b.o -o $(BINDIR)/strong_strong 2> $(LOGDIR)/strong_strong.stderr && \
		echo "UNEXPECTED: link succeeded (toolchain behavior differs?)" ) || \
		( echo "EXPECTED: link failed. See $(LOGDIR)/strong_strong.stderr")

# ------------------------------------
# Case 2: Weak variable vs Strong variable -> strong wins
# ------------------------------------
case_weak_strong_var: dirs
	@echo "== Case: weak var vs strong var (strong should win) =="
	@$(CC) $(CFLAGS) -c src/main.c		-o $(OBJDIR)/main.o
	@$(CC) $(CFLAGS) -c src/weak_var.c	-o $(OBJDIR)/weak_var.o
	@$(CC) $(CFLAGS) -c src/strong_var.c	-o $(OBJDIR)/strong_var.o
	# Provide a default implementation of hook() so main can link.
	@$(CC) $(CFLAGS) -c src/weak_func.c	-o $(OBJDIR)/weak_func.o
	@$(CC) $(LDFLAGS) $(OBJDIR)/main.o $(OBJDIR)/weak_var.o $(OBJDIR)/strong_var.o $(OBJDIR)/weak_func.o -o $(BINDIR)/weak_strong_var
	@$(NM) -a $(BINDIR)/weak_strong_var > $(LOGDIR)/weak_strong_var.nm.txt
	@$(READELF) -s $(BINDIR)/weak_strong_var > $(LOGDIR)/weak_strong_var.readelf-s.txt

# ------------------------------------
# Case 3: Weak function vs Strong function -> strong wins
# ------------------------------------
case_weak_strong_func: dirs
	@echo "== Case: weak func vs strong func (strong should win) =="
	@$(CC) $(CFLAGS) -c src/main.c		-o $(OBJDIR)/main.o
	@$(CC) $(CFLAGS) -c src/weak_func.c	-o $(OBJDIR)/weak_func.o
	@$(CC) $(CFLAGS) -c src/strong_func.c	-o $(OBJDIR)/strong_func.o
	# Provide a default definition of g_value so main can link.
	@$(CC) $(CFLAGS) -c src/weak_var.c      -o $(OBJDIR)/weak_var.o
	@$(CC) $(LDFLAGS) $(OBJDIR)/main.o $(OBJDIR)/weak_func.o $(OBJDIR)/strong_func.o $(OBJDIR)/weak_var.o -o $(BINDIR)/weak_strong_func
	@$(NM) -a $(BINDIR)/weak_strong_func > $(LOGDIR)/weak_strong_func.nm.txt
	@$(READELF) -s $(BINDIR)/weak_strong_func > $(LOGDIR)/weak_strong_func.readelf-s.txt

# ------------------------------------
# Case 4: Tentative definitions with -fcommon -> link succeeds (historical behavior)
# ------------------------------------
case_common_fcommon: dirs
	@echo "== Case: tentative defs with -fcommon (expect link success) =="
	@$(CC) $(CFLAGS) -fcommon -c src/common_a.c -o $(OBJDIR)/common_a_fcommon.o
	@$(CC) $(CFLAGS) -fcommon -c src/common_b.c -o $(OBJDIR)/common_b_fcommon.o

	# Link as a relocatable object (no need for main / CRT startup files).
	@ld -r $(OBJDIR)/common_a_fcommon.o $(OBJDIR)/common_b_fcommon.o -o $(OBJDIR)/common_merged_fcommon.o

	@$(NM) -a $(OBJDIR)/common_merged_fcommon.o > $(LOGDIR)/common_fcommon.nm.txt
	@$(READELF) -s $(OBJDIR)/common_merged_fcommon.o > $(LOGDIR)/common_fcommon.readelf-s.txt

# -----------------------------------
# Case 5: Tentative definitions with -fno-common -> link error (modern default in GCC 10+)
# -----------------------------------
case_common_fnocommon: dirs
	@echo "== Case: tentative defs with -fno-common (expect link failure) =="
	@$(CC) $(CFLAGS) -fno-common -c src/common_a.c -o $(OBJDIR)/common_a_fnocommon.o
	@$(CC) $(CFLAGS) -fno-common -c src/common_b.c -o $(OBJDIR)/common_b_fnocommon.o
	@set -e; \
	( $(CC) $(LDFLAGS) $(OBJDIR)/common_a_fnocommon.o $(OBJDIR)/common_b_fnocommon.o -o $(BINDIR)/common_fnocommon 2> $(LOGDIR)/common_fnocommon.stderr && \
		echo "UNEXPECTED: link succeeded (toolchain behavior differs?)" ) || \
		( echo "EXPECTED: link failed. See $(LOGDIR)/common_fnocommon.stderr" )

inspect:
	@echo "Logs are under $(LOGDIR)/"
	@ls -1 $(LOGDIR) || true
