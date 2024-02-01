SNDEGD_SRCS = $(sort alsa.c getrandom.c snd-egd.c nk/privs.c rb.c)
SNDEGD_OBJS = $(SNDEGD_SRCS:.c=.o)
SNDEGD_DEP = $(SNDEGD_SRCS:.c=.d)
INCL = -I.

CFLAGS = -MMD -O2 -s -fno-strict-overflow -pedantic -Wall -Wextra -Wimplicit-fallthrough=0 -Wformat=2 -Wformat-nonliteral -Wformat-security -Wshadow -Wpointer-arith -Wmissing-prototypes -Wcast-qual -Wsign-conversion -D_GNU_SOURCE
#-fsanitize=undefined -fsanitize-undefined-trap-on-error -fsanitize=address
CPPFLAGS += $(INCL)

all: snd-egd

snd-egd: $(SNDEGD_OBJS)
	$(CC) $(CFLAGS) $(INCL) -lasound -o $@ $^

-include $(SNDEGD_DEP)

clean:
	rm -f $(SNDEGD_OBJS) $(SNDEGD_DEP) snd-egd

.PHONY: all clean

