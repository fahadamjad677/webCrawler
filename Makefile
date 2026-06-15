CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lcurl

TARGET  = crawler
SRCS    = src/main.c        \
          src/queue.c       \
          src/visited.c     \
          src/fetcher.c     \
          src/parser.c      \
          src/persistence.c \
          src/logger.c      \
          src/worker.c      \
          src/stats.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f data/visited.txt data/crawler.log
