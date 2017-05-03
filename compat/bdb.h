#ifndef BDB_H
#define BDB_H

/* use shipped bdb header on linux, system bdb otherwise */
#ifdef __linux__
#include "db.h"
#else
#include <db.h>
#endif

#endif
