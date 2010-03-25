VERSION = 1.0
OPT_FLAGS = -Os -std=gnu99
WARNFLAGS = -Wall -pedantic
CFLAGS += $(WARNFLAGS) $(OPT_FLAGS)
LFLAGS = -lm -lasound

TARGETS = snd-egd

all: $(TARGETS) 

snd-egd: util.o log.o rb.o snd-egd.o
	$(CC) -o $@ $^ $(LFLAGS) 

install: snd-egd
	cp snd-egd /usr/local/sbin/
	cp init.d-snd-egd /etc/init.d/

clean:
	rm -f *.o core $(TARGETS)

package: clean
	# source package
	rm -rf snd-egd-$(VERSION)
	mkdir snd-egd-$(VERSION)
	cp *.c *.h TODO Makefile init.d-snd-egd COPYING README* snd-egd-$(VERSION)
	tar czf snd-egd-$(VERSION).tgz snd-egd-$(VERSION)
	rm -rf snd-egd-$(VERSION)
