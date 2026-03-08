CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11
LDFLAGS :=

SRC := src/main.c
OUT := artifacts/bin/syscall_cost

.PHONY: all clean dirs run

all: dirs $(OUT)

dirs:
	mkdir -p artifacts/bin artifacts/data

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data
