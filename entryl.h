#ifndef ENTRYL_H
#define ENTRYL_H

#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <form.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "shared.h"
#include "prefix_match.h"

#define MAXDESCR 16 * 1024

/* entry form */
typedef struct {
  time_t start;
  time_t end;
  char proj[MAXPROJ];
  char fname[30]; /* name of the file containing the summary */
  const char **tab_proj; /* tab copmletion list */
} entryl_t;

enum lprompt { LERROR = -1, LSAVE, LCANCEL, LDELETE };

int entryl(entryl_t *el, size_t line, const char *proj, const char **tab_proj, const time_t start, const time_t end, const char *dataroot, const char *fname, int proj_opt);

#endif
