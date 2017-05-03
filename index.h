#ifndef INDEX_H
#define INDEX_H

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "compat/bdb.h"
#include "entryl.h"
#include "shared.h"

#define MAXKEYSIZE (1 + MAXPROJ + 1 + sizeof(uint32_t) + sizeof(uint32_t))

/* iterator options */
typedef struct {
  char *proj;
  time_t minstart;
  time_t maxstart;
  int includemin;
  int includemax;
  size_t limit;
  size_t skip;
  int reverse;
  const DBT *offset; /* optional offset by key, bounded by minstart and maxstart */
} idx_itopts_t;

int idx_open(char *dp, char *idxpath, int ensure_new);
void idx_close(void);
DBT *idx_copy_key(const DBT *key);
int idx_free_key(const DBT **key);
time_t idx_pkey_start(const DBT *key);
time_t idx_pkey_end(const DBT *key);
char *idx_key_proj(const DBT *key);
time_t idx_key_start(const DBT *key);
time_t idx_key_end(const DBT *key);
int idx_keycmp(const DBT *key1, const DBT *key2);
int idx_iterate(const idx_itopts_t *opts, int (*cb)(DBT *), DBT **last_seen);
char *idx_key_info(const DBT *key);

char **idx_uniq_proj(void);
int idx_count(const idx_itopts_t *opts, int *count, int *summ);
int idx_del_by_key(const DBT *key);
FILE *idx_open_project_file(const DBT *key);
void idx_read_project_file(char *dst, size_t dstsize, const DBT *key);
int idx_save_project_file(const entryl_t *el, const DBT *key, DBT **pkey, DBT **dkey);

#endif
