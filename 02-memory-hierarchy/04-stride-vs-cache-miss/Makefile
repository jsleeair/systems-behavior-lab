CC := gcc
CFLAGS := -O2 -march=native -Wall -Wextra -std=c11
LDFLAGS :=

TARGET := artifacts/bin/stride_vs_cache_miss
SRC := src/stride_vs_cache_miss.c

.PHONY: all clean run dirs

all: $(TARGET)

dirs:
	mkdir -p artifacts/bin
	mkdir -p artifacts/data

$(TARGET): $(SRC) | dirs
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	rm -rf artifacts/bin artifacts/data
