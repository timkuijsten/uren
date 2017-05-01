#ifndef UREN_H
#define UREN_H

#include <err.h>
#include <libgen.h>
#include <locale.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "screen.h"
#include "index.h"

#define DATADIR ".uren"
#define IDXPATH ".cache"
#define MAXUSER 100

#ifndef PATH_MAX
  #error PATH_MAX must be defined
#endif

/* shell specific user info */
typedef struct {
  char name[MAXUSER];
  char home[PATH_MAX];
} user_t;

#endif
