SNDEGD_SRCS = $(sort $(wildcard *.c) $(wildcard nk/*.c))
SNDEGD_OBJS = $(SNDEGD_SRCS:.c=.o)
INCL = -I.

CC ?= gcc
CFLAGS = -O2 -s -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -Wsign-conversion -DNK_USE_CAPABILITY

all: snd-egd

clean:
	rm -f *.o nk/*.o snd-egd

%.o: %.c
	$(CC) $(CFLAGS) $(INCL) -c -o $@ $^

snd-egd: $(SNDEGD_OBJS)
	$(CC) $(CFLAGS) $(INCL) -lasound -o $@ $^

.PHONY: all clean

