SNDEGD_SRCS = $(sort $(wildcard *.c) $(wildcard nk/*.c))
SNDEGD_OBJS = $(SNDEGD_SRCS:.c=.o)
SNDEGD_DEP = $(SNDEGD_SRCS:.c=.d)
INCL = -I.

CC ?= gcc
CFLAGS = -MMD -O2 -s -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -Wsign-conversion -DNK_USE_CAPABILITY

-include $(SNDEGD_DEP)

all: snd-egd

clean:
	rm -f $(SNDEGD_OBJS) $(SNDEGD_DEP) snd-egd

%.o: %.c
	$(CC) $(CFLAGS) $(INCL) -c -o $@ $^

snd-egd: $(SNDEGD_OBJS)
	$(CC) $(CFLAGS) $(INCL) -lasound -o $@ $^

.PHONY: all clean

