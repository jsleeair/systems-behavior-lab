# Makefile for 00-cache-line-effect

CC := gcc
CFLAGS := -O2 -march=native -Wall -Wextra -std=c11
LDFLAGS :=

TARGET := artifacts/bin/cache_line_effect
SRC := src/cache_line_effect.c

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p artifacts/bin

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	@rm -rf artifacts/bin artifacts/results.csv

.PHONY: asan

asan: dirs
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-march=native -Wall -Wextra -std=c11 $(SRC) -o $(TARGET)
