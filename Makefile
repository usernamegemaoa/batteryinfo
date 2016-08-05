CC=gcc
CFLAGS=-O3 -Wall
EXEC_NAME=batteryinfo
EXEC_DEST=/usr/local/bin
MANPAGE_DEST=/usr/local/share/man
SHELL=/bin/bash

$(EXEC_NAME): batteryinfo.c
	$(CC) $^ -o $@ $(CFLAGS)

batteryinfo.1.gz: batteryinfo.1
	@gzip -9c batteryinfo.1 > batteryinfo.1.gz

.PHONY: installdocs clean install uninstall

installdocs: batteryinfo.1.gz
	@install -vm644 batteryinfo.1.gz $(MANPAGE_DEST)/man1
	@chmod +x $(MANPAGE_DEST)/man1
	@mandb

clean:
	@rm -vf $(EXEC_NAME) batteryinfo.1.gz

install: $(EXEC_NAME) installdocs
	@install -vsD $(EXEC_NAME) $(EXEC_DEST)

uninstall:
	@rm -v $(EXEC_DEST)/$(EXEC_NAME) $(MANPAGE_DEST)/man1/batteryinfo.1.gz
	@mandb
