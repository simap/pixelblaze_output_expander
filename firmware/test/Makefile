# Host build of the parser and channel code. Run with `make`.

CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -O1 -I../src
SRCS = host_test.c ../src/parser.c ../src/channels.c ../src/crc32.c

host_test: $(SRCS) ../src/*.h
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

.PHONY: run clean
run: host_test
	./host_test

clean:
	rm -f host_test
