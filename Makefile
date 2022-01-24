SNDEGD_SRCS = $(sort $(wildcard *.c) $(wildcard nk/*.c))
SNDEGD_OBJS = $(SNDEGD_SRCS:.c=.o)
SNDEGD_DEP = $(SNDEGD_SRCS:.c=.d)
INCL = -I.

CFLAGS = -MMD -O2 -s -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -Wsign-conversion -DNK_USE_CAPABILITY
CPPFLAGS += $(INCL)

all: snd-egd

snd-egd: $(SNDEGD_OBJS)
	$(CC) $(CFLAGS) $(INCL) -lasound -o $@ $^

-include $(SNDEGD_DEP)

clean:
	rm -f $(SNDEGD_OBJS) $(SNDEGD_DEP) snd-egd

.PHONY: all clean

