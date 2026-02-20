CC=gcc
CFLAGS=-O0 -g -Wall -Wextra

all: app

app: main.o a.o b.o add.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o app
