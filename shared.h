#ifndef SHARED_H
#define SHARED_H

#include <ctype.h>
#include <db.h>
#include <err.h>
#include <form.h>
#include <limits.h>
#include <inttypes.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

#ifndef PATH_MAX
  #error PATH_MAX must be defined
#endif

#define MAXPROJ 30

#ifndef max
  #define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* generic form type, support at most 20 fields */
typedef struct {
  WINDOW *w, *sw;
  FORM *form;
  FIELD *field[30];
} form_t;

int use_count(size_t *count, char *countstr);
int rtrim(char *s);
FIELD **set_date_field(FIELD **field, const char *label, const int col, const int row, const time_t def);
int parse_date_field(FIELD **field, time_t *t);
void info_prompt(const char *msg);

#endif
