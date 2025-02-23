CC = $(CROSS_COMPILE)gcc

# Add -g (and optionally -O0) to produce debug symbols for Valgrind

CFLAGS ?= -Wall -Wextra -g -O0

SRCS = simple_stream_server.c \
	   server_utils.c \
	   connection_handler.c \
	   thread_list.c

OBJS = $(SRCS:.c=.o)

TARGET = simple_stream_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)