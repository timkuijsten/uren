uren: uren.c uren.h log.c log.h screen.c screen.h entryl.c entryl.h index.c index.h shared.c shared.h shorten.c shorten.h prefix_match.c prefix_match.h compat/reallocarray.c compat/compat.h
	ctags *.h *.c
	cc -Wall -O0 -g -std=c99 -o uren uren.c log.c screen.c entryl.c index.c shared.c shorten.c prefix_match.c compat/reallocarray.c -lform -lncurses

depend:
	cc -E -MM *.c > .depend

.PHONY: clean
clean:
	rm -f ${OBJS} ${COMPAT} uren *.o
