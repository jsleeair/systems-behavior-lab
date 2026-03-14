CC	:= gcc
CFLAGS	:= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -pthread
LDFLAGS	:= -pthread

TARGET	:= artifacts/bin/memory_ordering
SRC	:= src/main.c

.PHONY: all clean dirs run

all: dirs $(TARGET)

dirs:
	mkdir -p artifacts/bin artifacts/data

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./run.sh

clean:
	rm -rf artifacts/bin artifacts/data
