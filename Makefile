CC = $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra -Werror

all:
	$(CC) $(CFLAGS) simple_stream_server.c -o simple_stream_server

clean:
	rm -f simple_stream_server