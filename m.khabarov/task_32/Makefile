CC = gcc
CFLAGS = -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L
# Linux: -lrt нужен для старого glibc (< 2.34); на новом — не вредит.
# Solaris/Illumos: -lsocket -lnsl -lrt
LDFLAGS = -lrt

TARGETS = server client

all: $(TARGETS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o socket

.PHONY: all clean