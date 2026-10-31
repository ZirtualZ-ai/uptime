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

.PHONY: all clean check install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPFILES)

check: $(TARGET)
	./$(TARGET) --version
	./$(TARGET) --check-config uptime.conf.example

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(SYSCONFDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -m 644 uptime.conf.example $(DESTDIR)$(SYSCONFDIR)/uptime.conf.example

clean:
	rm -f $(OBJECTS) $(DEPFILES) $(TARGET)