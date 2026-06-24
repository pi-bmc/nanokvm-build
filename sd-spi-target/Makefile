CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11

.PHONY: test clean
test: test_host
	./test_host

test_host: test_host.c sd_spi_target.c sd_spi_target.h
	$(CC) $(CFLAGS) -o $@ test_host.c sd_spi_target.c

clean:
	rm -f test_host
