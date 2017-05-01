#ifndef SCREEN_H
#define SCREEN_H

#include <assert.h>
#include <db.h>
#include <err.h>
#include <ncurses.h>
#include <stdint.h>
#include <time.h>
#include "index.h"
#include "shorten.h"
#include "entryl.h"

#define MAXLINE 1024
#define MAXPROG 32

void vp_init(char *datapath);
int vp_start(void);

#endif
