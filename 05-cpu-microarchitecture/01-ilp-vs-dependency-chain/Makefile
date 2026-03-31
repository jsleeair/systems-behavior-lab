CC := gcc
CFLAGS := -O3 -march=native -std=c11 -Wall -Wextra -Wshadow -Wconversion -fno-omit-frame-pointer
LDFLAGS :=

TARGET := artifacts/bin/ilp_vs_dependency_chain
SRC := src/ilp_vs_dependency_chain.c

.PHONY: all clean dirs run

all: dirs $(TARGET)

dirs:
	mkdir -p artifacts/bin
	mkdir -p artifacts/data

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET) --iters 200000000 --repeats 5 --warmup 1 --pin-cpu 0

clean:
	rm -rf artifacts/bin/ilp_vs_dependency_chain
	rm -rf artifacts/data/ilp_vs_dependency_chain.csv
