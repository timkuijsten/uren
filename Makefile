OS=$(shell uname)

PROG=uren

COMPAT=""

ifeq (${OS},Linux)
	COMPAT=strlcat.o strlcpy.o reallocarray.o
endif

ifeq (${OS},Darwin)
	COMPAT=reallocarray.o
endif

ifndef USRDIR
	USRDIR=/usr/local
endif
BINDIR=$(USRDIR)/bin
MANDIR=$(USRDIR)/share/man

OBJ=uren.o log.o screen.o entryl.o index.o shared.o shorten.o prefix_match.o
CFLAGS=-Wall -O0 -g

ifeq (${OS},Linux)
	LDFLAGS=-L. -lform -lncurses -ldb
else
	LDFLAGS=-lform -lncurses
endif

INSTALL_DIR=install -dm 755
INSTALL_BIN=install -m 555
INSTALL_MAN=install -m 444

${PROG}: ${OBJ} ${COMPAT}
	ctags *.h *.c
	$(CC) ${CFLAGS} -o $@ ${OBJ} ${COMPAT} ${LDFLAGS}

%.o: %.c
	$(CC) ${CFLAGS} -c $<

%.o: compat/%.c
	$(CC) ${CFLAGS} -c $<

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_BIN} ${PROG} ${DESTDIR}${BINDIR}
	${INSTALL_MAN} ${PROG}.1 ${DESTDIR}${MANDIR}/man1

depend:
	$(CC) ${CFLAGS} -E -MM *.c > .depend

.PHONY: clean
clean:
	rm -f ${OBJ} ${COMPAT} uren
