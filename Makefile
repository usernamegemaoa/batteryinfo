CC=gcc
CFLAGS=-O3 -Wall
EXEC_NAME=batteryinfo
DESTDIR=/usr/local
EXEC_DEST=$(DESTDIR)/bin
MANPAGE_DEST=$(DESTDIR)/share/man
SHELL=/bin/bash

$(EXEC_NAME): batteryinfo.c
	$(CC) $^ -o $@ $(CFLAGS)

batteryinfo.1.gz: batteryinfo.1
	@gzip -9c batteryinfo.1 > batteryinfo.1.gz

.PHONY: installdocs clean install uninstall

installdocs: batteryinfo.1.gz
	@mkdir -p $(MANPAGE_DEST)/man1
	@cp batteryinfo.1.gz $(MANPAGE_DEST)/man1/batteryinfo.1.gz
	@chmod 655 $(MANPAGE_DEST)/man1/batteryinfo.1.gz
	@mandb

clean:
	@rm -vf $(EXEC_NAME) batteryinfo.1.gz

install: $(EXEC_NAME) installdocs
	@mkdir -p $(EXEC_DEST)
	@cp $(EXEC_NAME) $(EXEC_DEST)/$(EXEC_NAME)
	@chmod 755 $(EXEC_DEST)/$(EXEC_NAME)

uninstall:
	@rm -v $(EXEC_DEST)/$(EXEC_NAME) $(MANPAGE_DEST)/man1/batteryinfo.1.gz
	@mandb
