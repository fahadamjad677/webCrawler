CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lpthread -lcurl

TARGET  = crawler
OBJDIR  = obj
DATADIR = data

SRCS = src/main.c        \
       src/queue.c       \
       src/visited.c     \
       src/fetcher.c     \
       src/parser.c      \
       src/persistence.c \
       src/logger.c      \
       src/worker.c      \
       src/stats.c       \
       src/menu.c

OBJS = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)
	rm -rf $(DATADIR)