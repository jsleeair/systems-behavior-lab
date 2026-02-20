CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O0 -g
LDFLAGS :=

SRC := src/main.c src/symbols.c
OUT := app

ART := artifacts

.PHONY: all clean artifacts run inspect

all: $(OUT)

$(OUT): $(SRC) | artifacts
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

artifacts:
	mkdir -p $(ART)

run: $(OUT) | artifacts
	./$(OUT) > $(ART)/run.stdout 2> $(ART)/run.stderr || true

# Produce inspection outputs for both objects and final executable.
inspect: | artifacts
	# Build objects (separately) so we can inspect per-TU symbol tables.
	$(CC) $(CFLAGS) -c src/symbols.c -o $(ART)/symbols.o
	$(CC) $(CFLAGS) -c src/main.c -o $(ART)/main.o

	# nm views (objects files and executable)
	nm -a $(ART)/symbols.o > $(ART)/nm_symbols_o.txt
	nm -a $(ART)/main.o    > $(ART)/nm_main_o.txt
	nm -a $(OUT)           > $(ART)/nm_app.txt

	# readelf symbol table (wide output; -W avoids truncation)
	readelf -sW $(ART)/symbols.o > $(ART)/readelf_s_symbols_o.txt
	readelf -sW $(ART)/main.o    > $(ART)/readelf_s_main_o.txt
	readelf -sW $(OUT)           > $(ART)/readelf_s_app.txt

	# objdump symbol table
	objdump -t $(ART)/symbols.o > $(ART)/objdump_t_symbols_o.txt
	objdump -t $(ART)/main.o    > $(ART)/objdump_t_main_o.txt
	objdump -t $(OUT)           > $(ART)/objdump_t_app.txt

clean:
	rm -rf $(OUT) $(ART)
