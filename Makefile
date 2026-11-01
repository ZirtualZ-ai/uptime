CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Isrc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -pthread
LDFLAGS ?=
LDLIBS += -lncursesw -lsqlite3 -lm -pthread

SOURCES = src/main.c src/ui.c src/local.c src/config.c src/db.c src/process.c src/worker.c src/wol.c
OBJECTS = $(SOURCES:.c=.o)
DEPFILES = $(OBJECTS:.o=.d)
TARGET = uptime-console
PREFIX ?= /usr/local
SYSCONFDIR ?= /etc/uptime
DESTDIR ?=

.PHONY: all clean check debug install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPFILES)

check: $(TARGET)
	./$(TARGET) --version
	./$(TARGET) --check-config uptime.conf.example

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -pthread' all

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(SYSCONFDIR) $(DESTDIR)/var/lib/uptime
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -m 644 uptime.conf.example $(DESTDIR)$(SYSCONFDIR)/uptime.conf.example
	if [ ! -e $(DESTDIR)$(SYSCONFDIR)/uptime.conf ]; then install -m 640 uptime.conf.example $(DESTDIR)$(SYSCONFDIR)/uptime.conf; fi
	if [ -z "$(DESTDIR)" ] && [ -f /etc/alpine-release ]; then sh scripts/install-alpine-tty1.sh "$(PREFIX)" "$(SYSCONFDIR)"; fi

clean:
	rm -f $(OBJECTS) $(DEPFILES) $(TARGET)