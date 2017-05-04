#include "index.h"

static int walk_datadir(char *idxpath, int(*cb)(const char proj[MAXPROJ], char *file, DBT **pkey, DBT **dkey));
static int key_within_bounds(const DBT *key);
static int is_d(const DBT *key);
static int is_p(const DBT *key);
static char *pkey_proj(const DBT *key);
static time_t pkey_start(const DBT *key);
static time_t pkey_end(const DBT *key);
static char *dkey_proj(const DBT *key);
static time_t dkey_start(const DBT *key);
static time_t dkey_end(const DBT *key);
static int in_drange(const DBT *key);
static int in_prange(const DBT *key);
static int timetostr(char *dst, const time_t src, const size_t dstsize);
static int prange_start(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t min);
static int prange_end(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t max);
static int drange_start(DBT *key, char *data, const size_t datasize, const time_t min);
static int drange_end(DBT *key, char *data, const size_t datasize, const time_t max);
static int pkey_make(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t start, const time_t end);
static int dkey_make(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t start, const time_t end);
static int dtopkey(DBT *pkey, char *pkeydata, const DBT *dkey, size_t pkeydatalen);
static size_t proj_len(const DBT *key);
static void free_uniq_proj(void);
static int idx_put(const char proj[MAXPROJ], char *file, DBT **pkey, DBT **dkey);
static int idx_del(const DBT *dkey, const DBT *pkey);
static int project_exists(const char *name);
static int ensure_project_exists(const char name[MAXPROJ]);
static void iterate(const DBT *min, int gte, const DBT *max, int lte, size_t limit, size_t skip, int reverse, int (*cb)(DBT *), DBT **last_seen);

/* used for global summation of minutes in index */
static double mtotal;
/* used for global counting of entries */
static int ecount;

/* used for finding all uniq project names */
static char **proj_names = NULL;
static size_t proj_name_next = 0;

static char p[PATH_MAX] = "";
static struct {
  char *str;
  int len;
  int fd;
} datapath = {
  p,
  0,
  -1
};

static DB *idx;

/*
 * Key formats. An uint32be is in network byte order or big endian.
 *
 * key      ::=  subkey ""
 * subkey   ::=
 *            |  pkey                     Project key, always starts with "P"
 *            |  dkey                     Date key, always starts with "D"
 * pkey     ::=  "\x50" string time time  "P" followed by the project name, then
 *                                        the start date and then the end date.
 *                                        Maps to a unique filename. "P" is in
 *                                        big endian.
 * dkey     ::=  "\x44" time time string  "D" followed by a start date, end date
 *                                        and at last the project name. Maps to
 *                                        a unique filename. "D" is in big
 *                                        endian.
 * string   ::=  (byte+) "\x00"           String - (byte+) is one or more ASCII
 *                                        encoded characters and must not
 *                                        contain a '\x00' or '\x01' byte.
 * uint32be ::=  sizeof(uint32_t)         system type uint32_t in network byte
 *                                        order
 * time     ::=  uint32be                 seconds since epoch
 *
 * filename ::= stime_stime               filenames on the disk, which the keys
 *                                        are based on, consist of two 14
 *                                        character ISO8601 strings representing
 *                                        UTC date and time separated by an
 *                                        underscore, yielding 29 chars. Each
 *                                        file is located in a directory that
 *                                        represents the project name.
 *
 * The keys have no values since all the data is in the keys.
 */

/*
 * Open a new or existing btree and ensure it contains indices for all files.
 * Initializes local copy of a db and datapath. It is ensured that datapath ends
 * with a trailing "/". Furthermore the file is locked for writing.
 *
 * Return 0 on succes, -1 on error.
 */
int
idx_open(char *dp, char *idxpath, int ensure_new)
{
  int fd, flags;
  int created = 0;
  struct flock lock;

  flags = 0 | O_RDWR;
  if (ensure_new)
    flags |= O_TRUNC;

  if ((datapath.len = strlcpy(datapath.str, dp, PATH_MAX)) > PATH_MAX)
    err(1, "%s strlcpy", __func__);
  // ensure trailing "/"
  if (datapath.str[datapath.len] != '\0')
    errx(1, "%s: expected null-byte in datapath", __func__);
  if (datapath.str[datapath.len - 1] != '/') {
    if (datapath.len + 1 >= PATH_MAX)
      errx(1, "%s: datapath too small for trailing '/'", __func__);
    // append "/"
    datapath.str[datapath.len++] = '/';
    datapath.str[datapath.len++] = '\0';
  }

  /* ensure a data dir exists */
  if (mkdir(datapath.str, 0700) == -1)
    if (errno != EEXIST)
      err(1, "%s: mkdir", __func__);

  /* save an open descriptor to the datadir */
  if ((datapath.fd = open(datapath.str, O_RDONLY)) == -1)
    err(1, "%s: open", __func__);

  /* open a btree for writing */
  if ((idx = dbopen(idxpath, flags, 0600, DB_BTREE, NULL)) == NULL) {
    if (errno == ENOENT) { /* retry with O_CREAT */
      if ((idx = dbopen(idxpath, flags | O_CREAT, 0600, DB_BTREE, NULL)) == NULL)
        err(1, "%s: dbopen: %s", __func__, idxpath);
      created = 1;
    } else {
      err(1, "%s: dbopen: %s", __func__, idxpath);
    }
  }

  /* and lock it */
  if ((fd = idx->fd(idx)) == -1)
    err(1, "%s: idx->fd", __func__);

  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  if (fcntl(fd, F_GETLK, &lock) == -1)
    err(1, "%s: fcntl", __func__);

  if (lock.l_type != F_UNLCK)
    errx(1, "already running: %d", lock.l_pid);

  lock.l_type = F_WRLCK;
  if (fcntl(fd, F_SETLK, &lock) == -1)
    err(1, "%s: fcntl failed to lock db", __func__);

  if (created)
    if (walk_datadir(idxpath, idx_put) < 0)
      errx(1, "%s: can't initialize index", __func__);

  return 0;
}

/*
 * Close a db.
 *
 * Return 0 on success, -1 on error.
 */
void
idx_close(void)
{
  if (close(datapath.fd) == -1)
    err(1, "%s: close", __func__);
  if (idx->close(idx) == -1)
    err(1, "%s: idx->close", __func__);
}

/*
 * Try to read every project directory and file in path and create two indices,
 * one by start date for date range queries and one by project + start date for
 * project based date range queries.
 *
 * Return 1 if index is created, 0 if no directory is found or exit on failure.
 */
static int
walk_datadir(char *idxpath, int(*cb)(const char proj[MAXPROJ], char *file, DBT **pkey, DBT **dkey))
{
  DIR *dir, *dir2;
  struct dirent *direntry, *file;
  int fd1, fd2;

  /* read all dirs in the directory */
  if ((dir = opendir(datapath.str)) == NULL)
    err(2, "%s: opendir", __func__);

  if ((fd1 = dirfd(dir)) == -1)
    err(1, "%s: dirfd", __func__);

  /* iterate over data dir, expect project directories */
  while ((direntry = readdir(dir)) != NULL) {
    /* skip hidden files, . and .. */
    if (direntry->d_name[0] == '.')
      continue;

    if ((fd2 = openat(fd1, direntry->d_name, O_RDONLY)) == -1)
      err(1, "%s: openat %s", __func__, direntry->d_name);

    /* open project directory */
    if ((dir2 = fdopendir(fd2)) == NULL) {
      log_warnx("%s: skip %s/%s", __func__, datapath.str, direntry->d_name);
      continue;
    }

    /* read project file names */
    while ((file = readdir(dir2)) != NULL) {
      /* skip hidden files, . and .. */
      if (file->d_name[0] == '.')
        continue;

      /* file name must consist of two ISO8601 dates */
      if (strlen(file->d_name) != 29) {
        log_warnx("%s: skip %s/%s/%s", __func__, datapath.str, direntry->d_name, file->d_name);
        continue;
      }

      /* expect UTC time notation */
      if (file->d_name[13] != 'Z' || file->d_name[28] != 'Z') {
        log_warnx("%s: skip %s/%s/%s", __func__, datapath.str, direntry->d_name, file->d_name);
        continue;
      }

      /* both dates msut be separated by an '_' */
      if (file->d_name[14] != '_') {
        log_warnx("%s: skip %s/%s/%s", __func__, datapath.str, direntry->d_name, file->d_name);
        continue;
      }

      if (cb(direntry->d_name, file->d_name, NULL, NULL) != 0) {
        log_warnx("%s: index error %s/%s/%s", __func__, datapath.str, direntry->d_name, file->d_name);
        continue;
      }
    }

    if (closedir(dir2) == -1)
      err(1, "%s: closedir dir2", __func__);
  }

  if (closedir(dir) == -1)
    err(1, "%s: closedir dir", __func__);

  return 0;
}

/*
 * Check if the is within bounds. Expect at least a project name of one
 * character.
 *
 * Return -1 if too small, 1 if too big, 0 if within bounds.
 */
static int
key_within_bounds(const DBT *key)
{
  if (key->size < 1 + 1 + 1 + sizeof(uint32_t) + sizeof(uint32_t))
    return -1;
  if (key->size > MAXKEYSIZE)
    return 1;

  return 0;
}

/*
 * Check if the key starts with a D.
 *
 * Return > 0 if this is a dkey, or 0 if it is not.
 */
static int
is_d(const DBT *key)
{
  if (key && key->size)
    return ((char *)key->data)[0] == 'D';

  return 0;
}

/*
 * Check if the key starts with a D or E.
 *
 * Return > 0 if this is a dkey, or 0 if it is not.
 */
static int
in_drange(const DBT *key)
{
  unsigned char c;

  if (!key->size)
    return 0;

  c = ((char *)key->data)[0];

  if (c == 'D')
    return 1;

  if (c == 'E' && key->size == 1)
    return 1;

  return 0;
}

/*
 * Check if the key starts with a P or Q.
 *
 * Return > 0 if this is a pkey, or 0 if it is not.
 */
static int
in_prange(const DBT *key)
{
  unsigned char c;

  if (!key->size)
    return 0;

  c = ((char *)key->data)[0];

  if (c == 'P')
    return 1;

  if (c == 'Q' && key->size == 1)
    return 1;

  return 0;
}

/*
 * Check if the key starts with a P.
 *
 * Return > 0 if this is a pkey, or 0 if it is not.
 */
static int
is_p(const DBT *key)
{
  if (key && key->size)
    return ((char *)key->data)[0] == 'P';

  return 0;
}

/*
 * Create a key for the P index that starts at the given project and optionally
 * at the given start time. It is the callers responsibility to properly
 * allocate enough space. proj must be null terminated. projlen must be the
 * number of bytes that precede the first null byte. If proj is the empty
 * string, it is not appended to the key.
 *
 * If min is not 0, projlen must not be 0.
 *
 * Warning: this is not a valid pkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
prange_start(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t min)
{
  uint32_t m;
  size_t s;

  if (proj[projlen] != '\0') {
    log_warnx("%s: illegal project name: %zu", __func__, projlen);
    return -1;
  }

  if (min && !projlen) {
    log_warnx("%s: if min is not 0, projlen must not be 0", __func__);
    return -1;
  }

  if (min)
    s = 1 + projlen + 1 + sizeof(uint32_t);
  else if (projlen)
    s = 1 + projlen + 1;
  else
    s = 1;

  if (datasize < s) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'P';

  /* only terminate if proj is given */
  if (projlen) {
    memcpy(data + 1, proj, projlen + 1);

    if (min) {
      m = htonl(min);
      memcpy(data + 1 + projlen + 1, &m, sizeof(m));
    }
  }

  key->data = data;
  key->size = s;

  return 0;
}

/*
 * Create a key for the P index that ends at the given project and optionally
 * at the given end time. It is the callers responsibility to properly allocate
 * enough space. proj must be null terminated. projlen must be the number of
 * bytes that precede the first null byte. If proj is the empty string, it is
 * not appended to the key.
 *
 * If max is not 0, projlen must not be 0.
 *
 * Warning: this is not a valid pkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
prange_end(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t max)
{
  uint32_t m;
  size_t s;

  if (proj[projlen] != '\0') {
    log_warnx("%s: illegal project name: %zu", __func__, projlen);
    return -1;
  }

  if (max && !projlen) {
    log_warnx("%s: if max is not 0, projlen must not be 0", __func__);
    return -1;
  }

  if (max)
    s = 1 + projlen + 1 + sizeof(uint32_t);
  else if (projlen)
    s = 1 + projlen + 1;
  else
    s = 1;

  if (datasize < s) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'P';

  /* only terminate if proj is given */
  if (projlen) {
    memcpy(data + 1, proj, projlen + 1);

    if (max) {
      m = htonl(max);
      memcpy(data + 1 + projlen + 1, &m, sizeof(m));
    } else {
      /* set null byte of project name to 0x01 */
      data[s - 1]++;
    }
  } else {
    /* set Q */
    data[s - 1]++;
  }

  key->data = data;
  key->size = s;

  return 0;
}

/*
 * Create a key for the D index that optionally starts at the given start time.
 * It is the callers responsibility to properly allocate enough space.
 *
 * Warning: this is not a valid dkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
drange_start(DBT *key, char *data, const size_t datasize, const time_t min)
{
  uint32_t m;
  size_t s;

  if (min)
    s = 1 + sizeof(uint32_t);
  else
    s = 1;

  if (datasize < s) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'D';

  if (min) {
    m = htonl(min);
    memcpy(data + 1, &m, sizeof(m));
  }

  key->data = data;
  key->size = s;

  return 0;
}

/*
 * Create a key for the D index that optionally ends at the given end time. It
 * is the callers responsibility to properly allocate enough space.
 *
 * Warning: this is not a valid dkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
drange_end(DBT *key, char *data, const size_t datasize, const time_t max)
{
  uint32_t m;
  size_t s;

  if (max)
    s = 1 + sizeof(uint32_t);
  else
    s = 1;

  if (datasize < s) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'D';

  if (max) {
    m = htonl(max);
    memcpy(data + 1, &m, sizeof(m));
  } else {
    /* set E */
    data[s - 1]++;
  }

  key->data = data;
  key->size = s;

  return 0;
}

/*
 * Create a valid pkey. It is the callers responsibility to properly allocate
 * enough space. proj must be null terminated. projlen must be the number of
 * bytes that precede the first null byte.
 *
 * Return 0 on success, -1 on error.
 */
static int
pkey_make(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t start, const time_t end)
{
  uint32_t m;

  if (proj[projlen] != '\0') {
    log_warnx("%s: illegal project name: %zu", __func__, projlen);
    return -1;
  }

  if (datasize < 1 + projlen + 1 + sizeof(uint32_t) + sizeof(uint32_t)) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'P';

  memcpy(data + 1, proj, projlen + 1);

  m = htonl(start);
  memcpy(data + 1 + projlen + 1, &m, sizeof(m));

  m = htonl(end);
  memcpy(data + 1 + projlen + 1 + sizeof(m), &m, sizeof(m));

  key->data = data;
  key->size = 1 + projlen + 1 + sizeof(m) + sizeof(m);

  return 0;
}

/*
 * Create a valid dkey. It is the callers responsibility to properly allocate
 * enough space. proj must be null terminated. projlen must be the number of
 * bytes that precede the first null byte.
 *
 * Return 0 on success, -1 on error.
 */
static int
dkey_make(DBT *key, char *data, const size_t datasize, const char *proj, const size_t projlen, const time_t start, const time_t end)
{
  uint32_t m;

  if (proj[projlen] != '\0') {
    log_warnx("%s: illegal project name: %zu", __func__, projlen);
    return -1;
  }

  if (datasize < 1 + sizeof(uint32_t) + sizeof(uint32_t) + projlen + 1) {
    log_warnx("%s: data size too small: %zu", __func__, datasize);
    return -1;
  }

  data[0] = 'D';

  m = htonl(start);
  memcpy(data + 1, &m, sizeof(m));

  m = htonl(end);
  memcpy(data + 1 + sizeof(m), &m, sizeof(m));

  memcpy(data + 1 + sizeof(m) + sizeof(m), proj, projlen + 1);

  key->data = data;
  key->size = 1 + sizeof(m) + sizeof(m) + projlen + 1;

  return 0;
}

/*
 * Convert a time to a string.
 *
 * On success, dst contains a 14 character ISO8601 UTC date and time.
 *
 * Return 0 on success, -1 on error.
 */
static int
timetostr(char *dst, const time_t src, const size_t dstsize)
{
  struct tm *bd;

  if (dstsize <= 14) {
    log_warnx("%s: dstsize too small: %zu", __func__, dstsize);
    return -1;
  }

  if ((bd = gmtime(&src)) == NULL)
    err(1, "%s: gmtime %lu", __func__, src);

  if (strftime(dst, dstsize, "%Y%m%dT%H%MZ", bd) == 0)
    errx(1, "%s: strftime: %zu, src %lu", __func__, dstsize, src);

  return 0;
}

DBT *
idx_copy_key(const DBT *key)
{
  DBT *r;
  if ((r = malloc(sizeof(DBT))) == NULL)
    err(1, "%s: malloc", __func__);
  if ((r->data = malloc(key->size)) == NULL)
    err(1, "%s: malloc data", __func__);
  r->size = key->size;
  memcpy(r->data, key->data, key->size);

  return r;
}

/* return 0 on success, -1 on error */
int
idx_free_key(const DBT **key)
{
  if (*key == NULL)
    return 0;

  free((*key)->data);
  free((void *)(*key));
  *key = NULL;

  return 0;
}

/* return pointer to project name in a key */
char *
idx_key_proj(const DBT *key)
{
  if (is_d(key))
    return dkey_proj(key);
  else if (is_p(key))
    return pkey_proj(key);

  errx(1, "%s: illegal key", __func__);

  return NULL;
}

/* return start date */
time_t
idx_key_start(const DBT *key)
{
  if (is_d(key))
    return dkey_start(key);
  else if (is_p(key))
    return pkey_start(key);

  errx(1, "%s: illegal key", __func__);

  return 0;
}

/* return end date */
time_t
idx_key_end(const DBT *key)
{
  if (is_d(key))
    return dkey_end(key);
  else if (is_p(key))
    return pkey_end(key);

  errx(1, "%s: illegal key", __func__);

  return 0;
}

/* return pointer to project name in dkey */
static char *
pkey_proj(const DBT *key)
{
  return &((char *)key->data)[1];
}

/* return start date */
static time_t
pkey_start(const DBT *key)
{
  uint32_t t;

  memcpy(&t, key->data + key->size - 2 * sizeof(t), sizeof(t));

  return (time_t)ntohl(t);
}

/* return end date */
static time_t
pkey_end(const DBT *key)
{
  uint32_t t;

  memcpy(&t, key->data + key->size - sizeof(t), sizeof(t));

  return (time_t)ntohl(t);
}

/* return pointer to project name in dkey */
static char *
dkey_proj(const DBT *key)
{
  return &((char *)key->data)[1 + sizeof(uint32_t) + sizeof(uint32_t)];
}

/* return start date */
static time_t
dkey_start(const DBT *key)
{
  uint32_t t;

  memcpy(&t, key->data + 1, sizeof(t));

  return (time_t)ntohl(t);
}

/* return end date */
static time_t
dkey_end(const DBT *key)
{
  uint32_t t;

  memcpy(&t, key->data + 1 + sizeof(t), sizeof(t));

  return (time_t)ntohl(t);
}

/*
 * Determine the number of characters that precede the terminating null byte of
 * the project name.
 */
static size_t
proj_len(const DBT *key)
{
  return key->size - sizeof(uint32_t) - sizeof(uint32_t) - 1;
}

/* recalculate all project names in a static buffer */
char **
idx_uniq_proj(void)
{
  int r;
  DBT key;
  char keydata[MAXKEYSIZE];
  char *name;

  free_uniq_proj();

  if (prange_start(&key, keydata, sizeof keydata, "", 0, 0) != 0)
    errx(1, "%s: prange_start", __func__);

  while ((r = idx->seq(idx, &key, NULL, R_CURSOR)) == 0) {
    proj_names = realloc(proj_names, (proj_name_next + 1) * sizeof(char *));
    name = pkey_proj(&key);
    proj_names[proj_name_next++] = strdup(name);

    if (prange_end(&key, keydata, sizeof keydata, name, strlen(name), 0) != 0)
      errx(1, "%s: prange_end", __func__);
  }
  if (r == -1)
    err(1, "%s: idx->seq set cursor", __func__);

  proj_names = realloc(proj_names, (proj_name_next + 1) * sizeof(char *));
  proj_names[proj_name_next] = NULL;

  return proj_names;
}

/* free proj_names and reset proj_name_next */
static void
free_uniq_proj(void)
{
  int i;

  if (proj_names == NULL)
    return;

  for (i = 0; proj_names[i]; i++) {
    free(proj_names[i]);
    proj_names[i] = NULL;
  }

  free(proj_names);
  proj_names = NULL;
  proj_name_next = 0;
}

/*
 * Three functions to calculate the total number of minutes in the index.
 *
 * uses GLOBALS: mtotal and ecount;
 */

/*
 * calculate the number of minutes a dkey spans
 *
 * NOTE: should only be used via idx_count().
 */
static int
summcount_d(DBT *dkey)
{
  if (!is_d(dkey))
    return 0;

  mtotal += difftime(dkey_end(dkey), dkey_start(dkey)) / 60;
  ecount++;

  return 1;
}

/*
 * calculate the number of minutes a pkey spans
 *
 * NOTE: should only be used via idx_count().
 */
static int
summcount_p(DBT *pkey)
{
  if (!is_p(pkey))
    return 0;

  mtotal += difftime(pkey_end(pkey), pkey_start(pkey)) / 60;
  ecount++;

  return 1;
}

/*
 * Calculate the total number of minutes and number of entries, optionally
 * filtered by project name, and/or minimum and/or maximum start time.
 *
 * count will contain the number of entries found
 * summ will contain the total number of minutes in all entries
 *
 * Return 0 on success, -1 on error.
 */
int
idx_count(const idx_itopts_t *opts, int *count, int *summ)
{
  mtotal = 0.0;
  ecount = 0;

  /* set default options */
  idx_itopts_t opt = {
    NULL, /* char *proj; */
    0, /* time_t minstart; */
    0, /* time_t maxstart; */
    0, /* int includemin; */
    0, /* int includemax; */
    0, /* size_t limit; */
    0, /* size_t skip; */
    0, /* int reverse; */
    NULL /* DBT *offset; */
  };

  if (!opts)
    opts = &opt;

  if (opts->proj && strlen(opts->proj))
    idx_iterate(opts, summcount_p, NULL);
  else
    idx_iterate(opts, summcount_d, NULL);

  *summ = mtotal;
  *count = ecount;

  return 0;
}

/*
 * Return pointer to string on success, or the empty string on error or if key
 * is NULL
 */
char *
idx_key_info(const DBT *key)
{
  static char dst[100];
  static char start[15];

  dst[0] = '\0';

  if (key == NULL)
    return dst;

  if (timetostr(start, idx_key_start(key), sizeof start) != 0)
    return dst;

  if (snprintf(dst, sizeof dst, "%c%c len: %zu, %s, %s", is_d(key) ? 'D' : ' ', is_p(key) ? 'P' : ' ', key->size, idx_key_proj(key), start) < 0)
    return dst;

  return dst;
}

/*
 * Iterate, optionally filtered by project name, and/or minimum and/or maximum
 * start time. Furthermore an offset can be used. See idx_itopts_t for options.
 *
 * NOTE: offset always overrules a min value or, in case reverse is true, a max
 * value.
 *
 * last_seen is optional and can be null.
 *
 * Return 0 on success, -1 on error.
 */
int
idx_iterate(const idx_itopts_t *opts, int (*cb)(DBT *), DBT **last_seen)
{
  DBT skey, ekey;
  const DBT *skeyp, *ekeyp;
  size_t l;
  char sdata[MAXKEYSIZE], edata[MAXKEYSIZE];

  /* set default options */
  idx_itopts_t opt = {
    NULL, /* char *proj; */
    0, /* time_t minstart; */
    0, /* time_t maxstart; */
    0, /* int includemin; */
    0, /* int includemax; */
    0, /* size_t limit; */
    0, /* size_t skip; */
    0, /* int reverse; */
    NULL /* DBT *offset; */
  };

  if (!opts)
    opts = &opt;

  /*
   * Check and set upper and lower bounds. If proj is set, use the P index,
   * otherwise use the D index.
   */
  if (opts->proj && (l = strlen(opts->proj))) {
    /* create a min key */
    if (!opts->reverse && opts->offset) {
      skeyp = opts->offset;
    } else {
      skeyp = &skey;
      if (prange_start(&skey, sdata, sizeof sdata, opts->proj, l, opts->minstart) != 0)
        errx(1, "%s: prange_start", __func__);
    }

    /* create a max key */
    if (opts->reverse && opts->offset) {
      ekeyp = opts->offset;
    } else {
      ekeyp = &ekey;
      if (prange_end(&ekey, edata, sizeof edata, opts->proj, l, opts->maxstart) != 0)
        errx(1, "%s: prange_end", __func__);
    }
  } else {
    /* create a min key */
    if (!opts->reverse && opts->offset) {
      skeyp = opts->offset;
    } else {
      skeyp = &skey;
      if (drange_start(&skey, sdata, sizeof sdata, opts->minstart) != 0)
        errx(1, "%s: drange_start", __func__);
    }

    /* create a max key */
    if (opts->reverse && opts->offset) {
      ekeyp = opts->offset;
    } else {
      ekeyp = &ekey;
      if (drange_end(&ekey, edata, sizeof edata, opts->maxstart) != 0)
        errx(1, "%s: drange_end", __func__);
    }
  }

  iterate(skeyp, opts->includemin, ekeyp, opts->includemax, opts->limit, opts->skip, opts->reverse, cb, last_seen);

  return 0;
}

/*
 * Compare two keys.
 *
 * return
 *     0 if keys are equal
 *    -1 if key1 != key2
 */
int
idx_keycmp(const DBT *key1, const DBT *key2)
{
  if (key1 == NULL || key2 == NULL)
    return -1;

  if (key1 == key2)
    return 0;

  if (key1->size == key2->size && memcmp(key1->data, key2->data, key2->size) == 0)
    return 0;

  return -1;
}

/*
 * Iterate over all entries, yielding the key. Works for both D and P indices.
 *
 * If no min and max are given, defaults to the D index. If a min and max are
 * given, they should be bound to the same index. If only min or max are given
 * the range is automatically bound.
 *
 * min: optional minimum key
 * gte: whether or not to include the minimum key itself
 * max: optional maximum key
 * lte: whether or not to include the maximum key itself
 * limit: the maximum number of yielded results
 * skip: skip the first number of results
 * reverse: emit in reverse
 * cb is called with each key that is within range
 * last_seen is set to the last seen key
 */
static void
iterate(const DBT *min, int gte, const DBT *max, int lte, size_t limit, size_t skip, int reverse, int (*cb)(DBT *), DBT **last_seen)
{
  DBT key, keymin, keymax;
  const DBT *bound;
  char keydata[MAXKEYSIZE], mindata[MAXKEYSIZE], maxdata[MAXKEYSIZE];
  int r, r2, proceed = 1, includebound;
  int linr; /* Last found key in range, needed for last_seen.
             * Keys that are either passed to the callback or skipped because of
             * "skip".
             */
  int d_range; /* whether this is a range on D or on P */
  int dir; /* direction, either R_NEXT or R_PREV */

  int cb_called = 0;

  /* do some range checking */
  d_range = 1;

  /* ensure the same range is used if any */
  if (min && max && min->size && max->size)
    if (in_drange(min) != in_drange(max))
      errx(1, "%s: min and max are not bound to the same index", __func__);

  /* determine which range */
  if (min && in_prange(min))
    d_range = 0;

  if (max && in_prange(max))
    d_range = 0;

  /* ensure a default range */
  if (!min || !min->size) {
    gte = 1;
    if (d_range) {
      if (drange_start(&keymin, mindata, sizeof mindata, 0) != 0)
        errx(1, "%s: drange_start", __func__);
    } else {
      if (prange_start(&keymin, mindata, sizeof mindata, "", 0, 0) != 0)
        errx(1, "%s: prange_start", __func__);
    }
    min = &keymin;
  }

  if (!max || !max->size) {
    lte = 1;
    if (d_range) {
      if (drange_end(&keymax, maxdata, sizeof maxdata, 0) != 0)
        errx(1, "%s: drange_start", __func__);
    } else {
      if (prange_end(&keymax, maxdata, sizeof maxdata, "", 0, 0) != 0)
        errx(1, "%s: prange_end", __func__);
    }
    max = &keymax;
  }

  if (min->size > sizeof keydata)
    errx(1, "%s: min key size is %zu and exceeds %lu", __func__, min->size, MAXKEYSIZE);

  /* seek to value that must be iterated first */
  if (reverse) {
    memcpy(keydata, max->data, max->size);
    key.data = keydata;
    key.size = max->size;
  } else {
    memcpy(keydata, min->data, min->size);
    key.data = keydata;
    key.size = min->size;
  }

  /* setting the cursor always ascends to the first match on prefix */
  if ((r2 = idx->seq(idx, &key, NULL, R_CURSOR)) == -1)
    err(1, "%s: idx->seq set cursor", __func__);

  /* set cursor to the first valid item, if any */
  if (reverse) {
    dir = R_PREV;
    /* set the first bound */
    bound = max;
    includebound = lte;
    /*
     * Setting the cursor is done in ascending order on prefix, there are three
     * scenarios:
     * 1. no key found
     *   ->  find next because it might hold min < key < max.
     * 2. if key.size != max.size (then key.size > max.size)
     *   ->  find next because it might hold min < key < max.
     * 3. a max key is found, and is a full match:
     *   ->  if lte is set, include, else find next
     */
    if (r2 == 0 && includebound && idx_keycmp(bound, &key) == 0) {
      /* key matches exactly with max and lte is set */
    } else {
      /* in all other cases find next */
      if ((r2 = idx->seq(idx, &key, NULL, dir)) == -1) {
        err(1, "%s: idx-> prev before max log_error", __func__);
      } else if (r2 == 1) {
        log_warnx("%s: idx->seq prev before max not found", __func__);
        return;
      } /* else: found, let it be processed by do while */
    }
    /* set the next bound */
    bound = min;
    includebound = gte;
  } else {
    dir = R_NEXT;
    /* set the first bound */
    bound = min;
    includebound = gte;
    /*
     * Setting the cursor is done in ascending order on prefix, there are two
     * scenarios:
     * 1. no min key found, we're done because there is no key > min.
     * 2. a min key is found
     *   a. if gte is set include the key since key >= min
     *   b. if !gte ensure that key > min:
     *     I. full match, exclude this key
     *     II. matches only on prefix, include this key
     */
    if (r2 == 1) { /* key itself nor any subsequent key is found */
      log_warnx("%s: idx->seq cursor not found", __func__);
      return;
    }
    if (!includebound && bound->size == key.size && memcmp(bound->data, key.data, key.size) == 0) {
      /* key matches exactly with min, fetch next */
      if ((r2 = idx->seq(idx, &key, NULL, dir)) == -1) {
        err(1, "%s: min next log_error", __func__);
      } else if (r2 == 1) {
        log_warnx("%s: idx->seq next after min not found", __func__);
        return;
      }
    }
    /* set the next bound */
    bound = max;
    includebound = lte;
  }

  linr = 0;
  do {
    /* see if we are already at or past the bound */
    /* first compare prefixes */
    if (bound->size < key.size) {
      r2 = memcmp(bound->data, key.data, bound->size);
    } else {
      r2 = memcmp(bound->data, key.data, key.size);
    }

    if (r2 == 0) { /* prefix matches */
      if (bound->size == key.size) { /* key matches exactly with bound */
        if (!includebound)
          break;
      } else if (bound->size > key.size) { /* bound > key */
        if (reverse)
          break;
      } else if (bound->size < key.size) { /* bound < key */
        if (!reverse)
          break;
      }
    } else if (r2 > 0) { /* bound prefix > key prefix */
      if (reverse)
        break;
    } else if (r2 < 0) { /* bound prefix < key prefix */
      if (!reverse)
        break;
    }

    linr = 1; /* the key is valid */

    if (skip > 0) {
      skip--;
    } else {
      proceed = cb(&key);
      cb_called++;
      if (limit == cb_called)
        break;
    }
  } while (proceed == 1 && (r = idx->seq(idx, &key, NULL, dir)) == 0);
  if (r == -1)
    err(1, "%s: idx->seq log_error", __func__);
  if (proceed == -1)
    err(1, "%s: cb log_error", __func__);

  if (linr && last_seen != NULL)
    *last_seen = idx_copy_key(&key);
}

/*
 * Check if a project already exists or not.
 *
 * Return 0 on success, 1 if the project does not exist, or -1 on error.
 */
static int
project_exists(const char *name)
{
  int r;
  DBT key, val;
  char data[100];

  if (pkey_make(&key, data, sizeof data, name, strlen(name), 0, 0) != 0)
    errx(1, "%s: pkey_make", __func__);

  if ((r = idx->seq(idx, &key, &val, R_CURSOR)) == -1)
    err(1, "%s: idx->seq set cursor", __func__);

  if (r == 1)
    return 1;

  if (strcmp(dkey_proj(&key), name) == 0)
    return 0;
  else
    return 1;
}

/* return 0 on success, -1 on error */
static int
ensure_project_exists(const char name[MAXPROJ])
{
  if (strchr(name, '/') != NULL) {
    log_warnx("%s: project name may not contain a '/'", __func__);
    return -1;
  }

  if (project_exists(name) == 0)
    return 0;

  // create directory
  if (mkdirat(datapath.fd, name, 0755) == -1)
    if (errno != EEXIST)
      err(1, "%s: mkdirat", __func__);

  return 0;
}

/*
 * Create the filename including a terminating null byte.
 *
 * Return 0 on success, -1 on error.
 */
static int
make_filename(char *dst, const time_t start, const time_t end, size_t dstlen)
{
  char *fp;

  if (dstlen < 30)
    return -1;

  fp = dst;
  if (timetostr(fp, start, dstlen - (fp - dst)))
    errx(1, "%s: timetostr 1", __func__);
  fp += 14;
  *fp++ = '_';
  if (timetostr(fp, end, dstlen - (fp - dst)))
    errx(1, "%s: timetostr 2", __func__);
  fp += 14;
  *fp++ = '\0';

  return 0;
}

/*
 * Convert a pkey to a dkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
ptodkey(DBT *dkey, char *dkeydata, const DBT *pkey, size_t dkeydatalen)
{
  char *proj = pkey_proj(pkey);
  return dkey_make(dkey, dkeydata, dkeydatalen, proj, strlen(proj), pkey_start(pkey), pkey_end(pkey));
}

/*
 * Convert a dkey to a pkey.
 *
 * Return 0 on success, -1 on error.
 */
static int
dtopkey(DBT *pkey, char *pkeydata, const DBT *dkey, size_t pkeydatalen)
{
  char *proj = dkey_proj(dkey);
  return pkey_make(pkey, pkeydata, pkeydatalen, proj, strlen(proj), dkey_start(dkey), dkey_end(dkey));
}

/*
 * Delete a project file.
 *
 * Return 0 on success, -1 on error.
 */
int
idx_del_by_key(const DBT *key)
{
  DBT okey; /* other key, depending on the parameter "key" */
  int fd;
  char fname[30], *proj, okeydata[MAXKEYSIZE];

  if (make_filename(fname, idx_key_start(key), idx_key_end(key), sizeof fname) == -1) {
    log_warnx("%s: make_filename", __func__);
    return -1;
  }

  proj = idx_key_proj(key);

  // remove the file
  if ((fd = openat(datapath.fd, proj, O_RDONLY)) == -1)
    err(1, "%s: openat", __func__);
  if (unlinkat(fd, fname, 0) == -1)
    err(1, "%s: unlinkat: %s/%s", __func__, proj, fname);
  if (close(fd) == -1)
    err(1, "%s: close", __func__);
  if (unlinkat(datapath.fd, proj, AT_REMOVEDIR) == -1)
    if (errno != ENOTEMPTY)
      err(1, "%s: unlinkat: %s", __func__, proj);

  if (is_d(key)) {
    if (dtopkey(&okey, okeydata, key, sizeof okeydata) == -1)
      errx(1, "%s: dtopkey", __func__);

    if (idx_del(key, &okey) != 0) {
      log_warnx("%s: idx_del", __func__);
      return -1;
    }
  } else if (is_p(key)) {
    if (ptodkey(&okey, okeydata, key, sizeof okeydata) == -1)
      errx(1, "%s: dtopkey", __func__);

    if (idx_del(&okey, key) != 0) {
      log_warnx("%s: idx_del", __func__);
      return -1;
    }
  } else {
    errx(1, "%s: illegal key", __func__);
  }
  idx->sync(idx, 0);

  return 0;
}

/*
 * Save a new or existing project file by entryl_t. If key is set, that key
 * will be replaced with the new entry in el.
 *
 * Copies the created keys and stores a pointer to the new pkey and dkey if not
 * NULL.
 *
 * Return 0 on success, -1 on error.
 */
int
idx_save_project_file(const entryl_t *el, const DBT *key, DBT **pkey, DBT **dkey)
{
  int fd;
  char fname[30];

  if (key != NULL) {
    if (idx_del_by_key(key) == -1) {
      log_warnx("%s: idx_del_by_key", __func__);
      return -1;
    }
  }

  if (el->proj[0] == '\0' || el->fname[0] == '\0')
    return -1;

  if (ensure_project_exists(el->proj) != 0) {
    log_warnx("%s: ensure_project_exists", __func__);
    return -1;
  }

  if (make_filename(fname, el->start, el->end, sizeof fname) == -1) {
    log_warnx("%s: make_filename", __func__);
    return -1;
  }

  // move the file
  if ((fd = openat(datapath.fd, el->proj, O_RDONLY)) == -1)
    err(1, "%s: openat", __func__);
  if (renameat(datapath.fd, el->fname, fd, fname) == -1)
    err(1, "%s: renameat: %s", __func__, el->fname);
  if (close(fd) == -1)
    err(1, "%s: close", __func__);

  if (idx_put(el->proj, fname, pkey, dkey) != 0) {
    log_warnx("%s: idx_put", __func__);
    return -1;
  }

  return 0;
}

/* read a file into dst and ensure null termination */
void
idx_read_project_file(char *dst, size_t dstsize, const DBT *key)
{
  FILE *pf;
  size_t n;

  if ((pf = idx_open_project_file(key)) == NULL)
    err(1, "%s: idx_open_project_file", __func__);

  if ((n = fread(dst, 1, dstsize, pf)) == 0)
    err(1, "%s: fread", __func__);

  /* try to add a trailing null, or replace last character with a null */
  if (n <= dstsize - 1)
    dst[n] = '\0';
  else
    dst[dstsize - 1] = '\0';

  /* trim any trailing newline */
  if (dst[n - 1] == '\n')
    dst[n - 1] = '\0';

  fclose(pf);
}

/*
 * Open a project file by key.
 *
 * FILE * on success, NULL on error
 */
FILE *
idx_open_project_file(const DBT *key)
{
  int projlen, offset;
  char pname[PATH_MAX], *pp;

  if (key_within_bounds(key) != 0)
    err(1, "%s: key out of bounds", __func__);

  if (datapath.len + key->size + (29 - 2 * sizeof(uint32_t)) >= sizeof pname)
    errx(1, "%s: path does not fit", __func__);

  pp = pname;
  if ((offset = strlcpy(pp, datapath.str, sizeof pname)) > sizeof pname)
    err(1, "%s: can't copy path", __func__);
  pp += offset;

  projlen = proj_len(key);
  memcpy(pp, idx_key_proj(key), projlen);
  pp += projlen - 1;

  // convert terminating null of project name to "/"
  *pp++ = '/';
  // append filename
  if (make_filename(pp, idx_key_start(key), idx_key_end(key), sizeof pname - (pp - pname)) == -1)
    errx(1, "%s: timetostr failed", __func__);

  // open the file
  return fopen(pname, "r");
}

/*
 * Create the index with both pkey and dkeys based on the directory and
 * filename.
 *
 * Both proj and file must be null terminated.
 *
 * proj is the name of the project, at most 255 characters, at least 1
 * file is the start and end date + time in ISO8601 format, UTC time and
 *   separated by an '_'. Thus must be exactly 29 characters.
 *
 * Both keys are added to the idx external variable and copied to pkey and dkey
 * if the pointers are not NULL.
 *
 * Return 0 on success, -1 on error.
 */
static int
idx_put(const char proj[MAXPROJ], char *file, DBT **pkey, DBT **dkey)
{
  DBT pk, dk;
  char keydata[MAXKEYSIZE];
  int projlen, filelen, r;
  struct tm dt;
  time_t start, end;

  projlen = strlen(proj);
  filelen = strlen(file);

  if (projlen > MAXPROJ)
    errx(1, "%s: project name too long: %d > %d. \"%s\"", __func__, projlen, MAXPROJ, proj);
  if (projlen < 1)
    errx(1, "%s: project name too short: %d < 1. \"%s\"", __func__, projlen, proj);
  if (filelen != 29)
    errx(1, "%s: illegal filename: %s", __func__, file);

  // determine start time
  if (strptime(file, "%Y%m%dT%H%MZ", &dt) == NULL)
    errx(1, "%s: could not parse start calendar time from filename: %s", __func__, file);

  // clear values that are not in the above time spec
  dt.tm_sec = 0;
  dt.tm_isdst = -1;

  // ensure the right week and year day are set
  start = timegm(&dt);

  // determine end time
  if (strptime(file + 14 + 1, "%Y%m%dT%H%MZ", &dt) == NULL)
    errx(1, "%s: could not parse end calendar time from filename: %s", __func__, file);

  // clear values that are not in the above time spec
  dt.tm_sec = 0;
  dt.tm_isdst = -1;

  // ensure the right week and year day are set
  end = timegm(&dt);

  /* P. project key */
  ////////////////////

  if (pkey_make(&pk, keydata, sizeof keydata, proj, projlen, start, end) == -1)
    errx(1, "%s: pkey_make", __func__);

  /* P. value */
  dk.data = NULL;
  dk.size = 0;

  if ((r = idx->put(idx, &pk, &dk, R_NOOVERWRITE)) == -1)
    err(1, "%s: put pk", __func__);
  if (r == 1)
    log_warnx("%s: duplicate pk %s/%s", __func__, proj, file);

  if (pkey != NULL)
    *pkey = idx_copy_key(&pk);

  /* D. date key */
  /////////////////

  if (dkey_make(&dk, keydata, sizeof keydata, proj, projlen, start, end) == -1)
    errx(1, "%s: dkey_make", __func__);

  /* D. value */
  pk.data = NULL;
  pk.size = 0;

  if ((r = idx->put(idx, &dk, &pk, R_NOOVERWRITE)) == -1)
    err(1, "%s: put dk", __func__);
  if (r == 1)
    log_warnx("%s: duplicate dk %s/%s", __func__, proj, file);

  if (dkey != NULL)
    *dkey = idx_copy_key(&dk);

  return 0;
}

/*
 * Delete the given entry from the index.
 *
 * Both proj and file must be null terminated.
 *
 * Return 0 on success, -1 on error.
 */
static int
idx_del(const DBT *dkey, const DBT *pkey)
{
  int r;

  if ((r = idx->del(idx, dkey, 0)) == -1)
    err(1, "%s: del dkey", __func__);
  if (r == 1) {
    log_warnx("%s: dkey not found %s", __func__, dkey_proj(dkey));
    return -1;
  }

  if ((r = idx->del(idx, pkey, 0)) == -1)
    err(1, "%s: del pkey", __func__);
  if (r == 1) {
    log_warnx("%s: pkey not found %s", __func__, dkey_proj(dkey));
    return -1;
  }

  return 0;
}
