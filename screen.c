#include "screen.h"

static int calc_status_line(int *count, int *summ);
static void update_status_line(int count, int summ);
static int filter_form(void);
static void enable_filter(char *proj, time_t start, time_t end);
static void disable_filter(void);
static int filter_enabled(void);
static int timer_started(void);
static int timer_start(void);
static int timer_stop(void);
static int timer_toggle(void);
static int ensure_key_storage(void);
static int get_idx(const DBT *key);
static const DBT *cur_get_key(void);
static int proj_filter_active(void);
static int add_entry_before(const DBT *ckey);
static int add_entry_after(const DBT *ckey);
static int ch_entry(const DBT *key);
static int rm_entry(const DBT *key);
static int reload_scr(const DBT *first);
static void cur_mv_down(uint32_t mv_lines);
static void cur_mv_up(uint32_t mv_lines);
static void cur_mv_line(uint32_t line);
static void cur_mv_key(const DBT *key);
static int move_lines(int mv_lines);
static void vp_mv_top(void);
static void vp_mv_bottom(void);
static int fetch_nkey(DBT *key);
static int print_key(int idx);
static int duration_in_hours(const time_t *start, const time_t *end, int *hours, int *minutes);
static void free_keys(int i);
int copy_file(const char *dataroot, const char *fname, FILE *src);

/* keep track of the total number of entries and the total number of minutes */
static int ecount, mtotal;

static const char tfile[] = ".timer";

/* use gfilter->fname[0] as an active flag */
static entryl_t gfilter;

// track collection of keys on the screen
static struct {
  const DBT **coll;
  size_t size;
} keys = {
  NULL,
  0
};

// temp storage of newly fetched keys used by fetch and print_key
static struct {
  const DBT **coll;
  size_t size;
  int nextw; /* point where next new element should be written */
} nkeys = {
  NULL,
  0,
  0
};

/* track width and height of the screen, and number of entry lines. These are
 * lines within the viewport that are actually used to display an entry. Status
 * lines are lines used to display other info than an entry.
 *
 * e_lines + s_lines = vp_lines.
 *
 * Updated after every key press.
 */
static int vp_lines, vp_cols, e_lines, s_lines = 2;
static char *datapath;

/* init the viewport, fill with entries */
void
vp_init(char *dp)
{
  datapath = dp;

  initscr();
  if (atexit((void (*)(void))endwin) != 0)
    errx(1, "%s: can't register endwin", __func__);
  scrollok(stdscr, TRUE);
  noecho();

  ensure_key_storage();
  if (calc_status_line(&ecount, &mtotal) != 0)
    errx(1, "%s: calc_status_line", __func__);

  // fetch new keys and move them to nkeys
  vp_mv_bottom();
  update_status_line(ecount, mtotal);
}

int
vp_start(void)
{
  int i, prevkey, resetprev, key, proceed;
  size_t count;
  char countstr[7];

  prevkey = 0;
  resetprev = 0;
  proceed = 1;
  countstr[0] = '\0';
  while(proceed && (key = getch()) != ERR) {
    //mvprintw(e_lines, 0, "%d %c ", key, key);

    // redetermine screen size and resize keys and nkeys if needed
    ensure_key_storage();

    switch (key) {
    case '0':
      /* don't prepend 0 to any following number */
      if (strlen(countstr) == 0)
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      i = strlen(countstr);
      if (i >= sizeof countstr - 1)
        break;
      countstr[i] = key;
      countstr[i + 1] = '\0';
      break;
    case 2: // ctrl-B, scroll a full screen up minus two items
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^B use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(-1 * count * e_lines - 2);
      break;
    case 5: // ctrl-E
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^E use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(count);
      break;
    case 6: // ctrl-F, scroll a full screen down minus two items
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^F use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(count * e_lines - 2);
      break;
    case 4: // ctrl-D, scroll half the screen down
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^D use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(count * e_lines / 2);
      break;
    case 21: // ctrl-U, scroll half the screen up
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^U use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(-1 * count * e_lines / 2);
      break;
    case 25: // ctrl-Y
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: ^Y use_count", __func__);
      if (i == 1)
        count = 1;
      move_lines(-1 * count);
      break;
    case 'd':
      if (prevkey == 'd') {
        rm_entry(cur_get_key());
        resetprev = 1;
      }
      break;
    case 'f':
      if (filter_enabled())
        disable_filter();
      else
        filter_form();
      break;
    case 'k':
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: j use_count", __func__);
      if (i == 1)
        count = 1;
      cur_mv_up(count);
      break;
    case 'j':
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: j use_count", __func__);
      if (i == 1)
        count = 1;
      cur_mv_down(count);
      break;
    case 'g':
      if (prevkey == 'g') {
        vp_mv_top();
        resetprev = 1;
      }
      break;
    case 'G':
      vp_mv_bottom();
      break;
    case 'H':
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: j use_count", __func__);
      if (i == 1)
        count = 1;
      cur_mv_line(count - 1);
      break;
    case 'M':
      cur_mv_line(e_lines / 2);
      break;
    case 'L':
      if ((i = use_count(&count, countstr)) == -1)
        errx(1, "%s: j use_count", __func__);
      if (i == 1)
        count = 1;
      cur_mv_line(e_lines - count);
      break;
    case 'q':
      proceed = 0;
      break;
    case 'O':
    case 'I':
      add_entry_before(cur_get_key());
      if (calc_status_line(&ecount, &mtotal) != 0)
        errx(1, "%s: calc_status_line", __func__);
      break;
    case 'i': /* alias for G + o */
      vp_mv_bottom();
    case 'A':
    case 'o':
      add_entry_after(cur_get_key());
      if (calc_status_line(&ecount, &mtotal) != 0)
        errx(1, "%s: calc_status_line", __func__);
      break;
    case 's':
      timer_toggle();
      break;
    case 'S':
      ch_entry(cur_get_key());
      break;
    case 'c':
      if (prevkey == 'c') {
        ch_entry(cur_get_key());
        resetprev = 1;
      }
      break;
    }
    update_status_line(ecount, mtotal);
    if (resetprev) {
      prevkey = 0xff; /* something unused */
      resetprev = 0;
    } else {
      prevkey = key;
    }
  }

  return 0;
}

/* show filter form */
static int
filter_form(void)
{
  entryl_t el;

  /* init times, and end to current time if set to 0 */
  el.start = gfilter.start;
  el.end = gfilter.end;
  if (el.end == 0)
    el.end = time(NULL);

  switch (entryl(&el, vp_lines - 1, gfilter.proj, (const char **)idx_uniq_proj(), el.start, el.end, NULL, NULL, 1)) {
  case LERROR:
    info_prompt("form error");
    break;
  case LSAVE:
    enable_filter(el.proj, el.start, el.end);
    break;
  case LDELETE:
    disable_filter();
    break;
  }

  return 0;
}

static void
enable_filter(char *proj, time_t start, time_t end)
{
  if (strlcpy(gfilter.proj, proj, sizeof gfilter.proj) > sizeof gfilter.proj)
    errx(1, "%s: strlcpy", __func__);
  gfilter.start = start;
  gfilter.end = end;
  gfilter.fname[0] = 1;

  reload_scr(NULL);
  if (calc_status_line(&ecount, &mtotal) != 0)
    errx(1, "%s: calc_status_line", __func__);
}

static void
disable_filter(void)
{
  gfilter.proj[0] = '\0';
  gfilter.fname[0] = 0;

  reload_scr(NULL);
  if (calc_status_line(&ecount, &mtotal) != 0)
    errx(1, "%s: calc_status_line", __func__);
}

/* return 1 if filter is active, 0 if not */
static int
filter_enabled(void)
{
  return gfilter.fname[0];
}

/* return number of seconds since epoch if timer is started, 0 if not, -1 on error */
static int
timer_started(void)
{
  struct stat st;
  char pname[PATH_MAX];

  if (snprintf(pname, sizeof pname, "%s/%s", datapath, tfile) >= sizeof pname)
    errx(1, "%s: snprintf", __func__);

  if (stat(pname, &st) == -1) {
    if (errno == ENOENT)
      return 0;
    else
      return -1;
  }

  if (st.st_ctime > time(NULL))
    errx(1, "%s: time is in the future %ld", __func__, st.st_ctime);

  return st.st_ctime;
}

/* start running a timer. 0 on success, -1 on error */
static int
timer_start(void)
{
  int fd;

  char pname[PATH_MAX];

  if (snprintf(pname, sizeof pname, "%s/%s", datapath, tfile) >= sizeof pname)
    errx(1, "%s: snprintf", __func__);

  if ((fd = open(pname, O_CREAT | O_EXCL, 0600)) == -1) {
    if (errno != EEXIST)
      err(1, "%s: open", __func__);
    else {
      /* timer already running */
      return -1;
    }
  }
  if (close(fd) == -1)
    err(1, "%s: close", __func__);

  return 0;
}

/*
 * stop a running timer.
 *
 * returns the number of seconds since the epoch when the timer was started
 * 0 if no timer was running
 * -1 on error
 */
static int
timer_stop(void)
{
  int s;
  char pname[PATH_MAX];

  if (snprintf(pname, sizeof pname, "%s/%s", datapath, tfile) >= sizeof pname)
    errx(1, "%s: snprintf", __func__);

  s = timer_started();

  if (unlink(pname) == -1) {
    if (errno == ENOENT && s == 0)
      return 0;
    else
      err(1, "%s: unlink", __func__);
  }

  return s;
}

/*
 * Either starts or stops a timer. If a timer was running, asks the user for
 * input with defaults set to the recorded time.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
timer_toggle(void)
{
  int s;
  DBT *pkey, *dkey;
  entryl_t el;

  s = timer_started();

  if (s < 0)
    err(1, "%s: timer_started", __func__);

  if (!s)
    return timer_start();

  /* first update displayed timer */
  if (calc_status_line(&ecount, &mtotal) != 0)
    errx(1, "%s: calc_status_line", __func__);
  update_status_line(ecount, mtotal);

  switch (entryl(&el, vp_lines - 1, NULL, (const char **)idx_uniq_proj(), s, time(NULL), datapath, ".add", 0)) {
  case LERROR:
    log_warnx("form error");
    return -1;
  case LSAVE:
    if (idx_save_project_file(&el, NULL, &pkey, &dkey) == -1)
      errx(1, "%s: idx_save_project_file", __func__);

    if (timer_stop() <= 0)
      err(1, "%s: timer_stop", __func__);

    /* reload and center around the added entry */
    reload_scr(proj_filter_active() ? pkey : dkey);
    move_lines(-1 * e_lines / 2);
    cur_mv_key(proj_filter_active() ? pkey : dkey);
    idx_free_key((const DBT **)&pkey);
    idx_free_key((const DBT **)&dkey);

    if (calc_status_line(&ecount, &mtotal) != 0)
      errx(1, "%s: calc_status_line", __func__);
    break;
  }

  return 0;
}

/* return 1 if a date filter is active, 0 if not */
static int
proj_filter_active(void)
{
  return filter_enabled() && gfilter.proj[0];
}

/*
 * Insert above the given key, using the same project, start and end time equal
 * the start time of the current key.
 */
static int
add_entry_before(const DBT *ckey)
{
  entryl_t el;
  DBT *pkey, *dkey;
  time_t start, end;
  char *proj;

  proj = NULL;
  if ((start = time(NULL)) == -1)
    errx(1, "%s time start", __func__);
  end = 0;

  if (ckey) {
    proj = idx_key_proj(ckey);
    start = idx_key_start(ckey);
    end = start;
  }
  switch (entryl(&el, vp_lines - 1, proj, (const char **)idx_uniq_proj(), start, end, datapath, ".add", 0)) {
  case LERROR:
    info_prompt("form error");
    break;
  case LSAVE:
    if (idx_save_project_file(&el, NULL, &pkey, &dkey) == -1)
      errx(1, "%s: idx_save_project_file", __func__);

    /* reload and center around the added entry */
    reload_scr(proj_filter_active() ? pkey : dkey);
    move_lines(-1 * e_lines / 2);
    cur_mv_key(proj_filter_active() ? pkey : dkey);
    idx_free_key((const DBT **)&pkey);
    idx_free_key((const DBT **)&dkey);

    if (calc_status_line(&ecount, &mtotal) != 0)
      errx(1, "%s: calc_status_line", __func__);
    break;
  }

  return 0;
}

/*
 * Insert under the current key, using the same project, start and end time
 * equal the end time of the current key.
 */
static int
add_entry_after(const DBT *ckey)
{
  DBT *pkey, *dkey;
  entryl_t el;
  time_t start;
  char *proj;

  /* get key under the cursor */
  proj = NULL;
  if ((start = time(NULL)) == -1)
    errx(1, "%s time start", __func__);

  if (ckey) {
    proj = idx_key_proj(ckey);
    start = idx_key_end(ckey);
  }
  switch (entryl(&el, vp_lines - 1, proj, (const char **)idx_uniq_proj(), start, 0, datapath, ".add", 0)) {
  case LERROR:
    info_prompt("form error");
    break;
  case LSAVE:
    if (idx_save_project_file(&el, NULL, &pkey, &dkey) == -1)
      errx(1, "%s: idx_save_project_file", __func__);

    /* reload and center around the added entry */
    reload_scr(proj_filter_active() ? pkey : dkey);
    move_lines(-1 * e_lines / 2);
    cur_mv_key(proj_filter_active() ? pkey : dkey);
    idx_free_key((const DBT **)&pkey);
    idx_free_key((const DBT **)&dkey);

    if (calc_status_line(&ecount, &mtotal) != 0)
      errx(1, "%s: calc_status_line", __func__);
    break;
  }

  return 0;
}

/*
 * Edit the given key.
 *
 * Return 0 on success, -1 on error.
 */
static int
ch_entry(const DBT *key)
{
  FILE *fp;
  entryl_t el;
  time_t start, end;
  char *proj;

  /* get the key under the cursor */
  if (key == NULL)
    return -1;

  proj = idx_key_proj(key);
  start = idx_key_start(key);
  end = idx_key_end(key);

  if ((fp = idx_open_project_file(key)) == NULL)
    errx(1, "%s: idx_open_project_file", __func__);
  if (copy_file(datapath, ".edit", fp) == -1)
    errx(1, "%s: copy_file", __func__);
  if (fclose(fp) == EOF)
    err(1, "%s: fclose", __func__);

  switch (entryl(&el, vp_lines - 1, proj, (const char **)idx_uniq_proj(), start, end, datapath, ".edit", 0)) {
  case LERROR:
    info_prompt("Form error");
    break;
  case LSAVE:
    if (idx_save_project_file(&el, key, NULL, NULL) == -1)
      errx(1, "%s: idx_save_project_file", __func__);

    /* reload and center around the changed entry */
    reload_scr(key);
    move_lines(-1 * e_lines / 2);
    cur_mv_key(key);

    if (calc_status_line(&ecount, &mtotal) != 0)
      errx(1, "%s: calc_status_line", __func__);
    break;
  }

  return 0;
}

static int
rm_entry(const DBT *key)
{
  /* get the key under the cursor */
  if (key == NULL)
    return -1;

  if (idx_del_by_key(key) == -1)
    errx(1, "%s: idx_del_by_key", __func__);

  /* reload and center around the deleted entry */
  reload_scr(key);
  cur_mv_key(keys.coll[0]);
  move_lines(-1 * e_lines / 2);

  if (calc_status_line(&ecount, &mtotal) != 0)
    errx(1, "%s: calc_status_line", __func__);

  return 0;
}

/*
 * Reload all keys on the screen, optionally starting at the given key.
 *
 * Return 0 on success, -1 on error.
 */
static int
reload_scr(const DBT *first)
{
  const DBT *offset;
  int i;

  if (first)
    offset = idx_copy_key(first);
  else
    offset = NULL;

  /* set iterator options */
  idx_itopts_t opts = {
    NULL, /* char *proj; */
    0, /* time_t minstart; */
    0, /* time_t maxstart; */
    1, /* int includemin; */
    0, /* int includemax; */
    keys.size, /* size_t limit; */
    0, /* size_t skip; */
    0, /* int reverse; */
    (DBT *)offset /* DBT *offset; */
  };
  if (filter_enabled()) {
    if (proj_filter_active())
      opts.proj = gfilter.proj;
    opts.minstart = gfilter.start;
    opts.maxstart = gfilter.end;
  }

  /* now free and fetch all keys */
  free_keys(0);
  idx_iterate(&opts, fetch_nkey, NULL);

  /* and free the copied offset */
  if (offset)
    idx_free_key(&offset);

  /* move newly found keys */
  for (i = 0; i < nkeys.nextw; i++) {
    assert(nkeys.coll[i]);
    assert(!keys.coll[i]);
    keys.coll[i] = nkeys.coll[i];
    nkeys.coll[i] = NULL;
  }
  nkeys.nextw = 0;

  /* and redraw the whole screen */
  for (int i = 0; i < keys.size; i++)
    print_key(i);

  return 0;
}

/* calculate number of entries and total number of minutes */
static int
calc_status_line(int *count, int *summ)
{
  /* set iterator options */
  idx_itopts_t opts = {
    NULL, /* char *proj; */
    0, /* time_t minstart; */
    0, /* time_t maxstart; */
    1, /* int includemin; */
    1, /* int includemax; */
    0, /* size_t limit; */
    0, /* size_t skip; */
    0, /* int reverse; */
    NULL /* DBT *offset; */
  };
  if (filter_enabled()) {
    if (proj_filter_active())
      opts.proj = gfilter.proj;
    opts.minstart = gfilter.start;
    opts.maxstart = gfilter.end;
  }

  return idx_count(&opts, count, summ);
}

/* use the status lines at the bottom of the screen */
static void
update_status_line(int count, int summ)
{
  int s, y;
  char sdout[64];

  getyx(stdscr, y, s);
  if ((s = timer_started()) == -1)
    errx(1, "%s: timer_started", __func__);

  if (s) {
    s = time(NULL) - s;
    if (mvprintw(e_lines, 0, " %d                            %2d:%02d    timer: %2d:%02d", count, summ / 60, summ % 60, s / 60, s % 60) == ERR)
      errx(1, "%s: mvprintw", __func__);
  } else {
    if (mvprintw(e_lines, 0, " %d                            %2d:%02d", count, summ / 60, summ % 60) == ERR)
      errx(1, "%s: mvprintw", __func__);
  }

  if (filter_enabled()) {
    if (printw("\t\t%s\t", gfilter.proj) == ERR)
      errx(1, "%s: printw project", __func__);

    if (strftime(sdout, sizeof sdout, "%R %d-%m-%Y", localtime(&gfilter.start)) == 0)
      err(1, "%s: could not format broken-down start time", __func__);
    if (printw("   %s", sdout) == ERR)
      errx(1, "%s: printw start", __func__);

    if (strftime(sdout, sizeof sdout, "%R %d-%m-%Y", localtime(&gfilter.end)) == 0)
      err(1, "%s: could not format broken-down end time", __func__);
    if (printw(" - %s", sdout) == ERR)
      errx(1, "%s: printw end", __func__);
  }
  clrtoeol();
  if (mvchgat(e_lines, 0, -1, A_REVERSE, 0, NULL) == ERR)
    errx(1, "%s: mvchgat ON", __func__);
  /* clear last line, expect two status lines */
  assert(s_lines == 2);
  move(e_lines + 1, 0);
  clrtoeol();
  move(y, 0);
}

/*
 * Determine viewport size, number of entry lines and status lines and ensure
 * key and nkeys are big enough for the current number of entry lines
 * (e_lines).
 */
static int
ensure_key_storage(void)
{
  int i, diff;

  /* check for curruption first: keys.size == nkeys.size == e_lines, */
  assert(keys.size == e_lines);
  assert(keys.size == nkeys.size);

  /* determine screen size and init keys and nkeys */
  getmaxyx(stdscr, vp_lines, vp_cols);

  /* keep the lines used for status info free */
  e_lines = vp_lines - s_lines;

  diff = e_lines - keys.size;

  if (diff == 0)
    return 0;

  /* free extraneous keys */
  if (diff < 0) {
    log_warnx("%s: free %d keys", __func__, diff);
    for (i = keys.size - 1; i >= e_lines; i--) {
      idx_free_key(&keys.coll[i]);
      idx_free_key(&nkeys.coll[i]);
    }
  }

  keys.coll = realloc(keys.coll, sizeof(DBT *) * e_lines);
  nkeys.coll = realloc(nkeys.coll, sizeof(DBT *) * e_lines);
  if (keys.coll == NULL || nkeys.coll == NULL)
    err(1, "%s: realloc", __func__);

  keys.size = e_lines;
  nkeys.size = e_lines;

  log_warnx("%s: %zu %zu", __func__, keys.size, nkeys.size);

  /* init new elements to null */
  if (diff > 0) {
    for (i = e_lines - 1; i >= e_lines - diff; i--) {
      keys.coll[i] = NULL;
      nkeys.coll[i] = NULL;
    }
  }

  return 0;
}

/* return the index of the key in the keys array or -1 if not found */
static int
get_idx(const DBT *key)
{
  int i;

  if (key == NULL)
    return -1;

  for (i = 0; i < keys.size && idx_keycmp(key, keys.coll[i]) != 0; i++)
    continue;

  if (i < keys.size)
    return i;

  return -1;
}

/*
 * Get the key under the cursor, if any.
 *
 * Return the key on success or NULL if not found.
 */
static const DBT *
cur_get_key(void)
{
  int y, i;

  getyx(stdscr, y, i);
  if (y < 0 || y >= keys.size || !keys.coll[y])
    return NULL;

  return keys.coll[y];
}

/* move the cursor down, and the viewport if necessary */
static void
cur_mv_down(uint32_t mv_lines)
{
  int x, y;

  getyx(stdscr, y, x);

  /* move viewport first if needed */
  if (y + mv_lines > e_lines - 1) {
    mv_lines -= (e_lines - 1 - y);
    move_lines(mv_lines);
    mv_lines = e_lines - 1 - y;
  }

  /* set current line to normal */
  if (chgat(-1, A_NORMAL, 0, NULL) == ERR)
    errx(1, "%s: mvchgat OFF", __func__);

  /* highlight new line */
  y += mv_lines;

  /* but only if line is non-null */
  while (y > 0 && !keys.coll[y])
    y--;

  if (mvchgat(y, 0, -1, A_REVERSE, 0, NULL) == ERR)
    errx(1, "%s: mvchgat ON", __func__);
}

/* move the cursor up */
static void
cur_mv_up(uint32_t mv_lines)
{
  int x, y;

  getyx(stdscr, y, x);

  /* if the viewport needs to be moved, do that first */
  if (mv_lines > y) {
    mv_lines -= y;
    move_lines(-1 * mv_lines);
    mv_lines = y;
  }

  /* set current line to normal */
  if (chgat(-1, A_NORMAL, 0, NULL) == ERR)
    errx(1, "%s: mvchgat OFF", __func__);

  /* highlight new line */
  y -= mv_lines;

  /* but only if line is non-null */
  while (y > 0 && !keys.coll[y])
    y--;

  if (mvchgat(y, 0, -1, A_REVERSE, 0, NULL) == ERR)
    errx(1, "%s: mvchgat ON", __func__);
}

/* move to a line relative to the current window */
static void
cur_mv_line(uint32_t line)
{
  int x, y;

  if (line > e_lines - 1)
    line = e_lines - 1;

  getyx(stdscr, y, x);

  if (y < line)
    cur_mv_down(line - y);
  else
    cur_mv_up(y - line);
}

/* move to the line that represents the given key */
static void
cur_mv_key(const DBT *key)
{
  int i;
  if ((i = get_idx(key)) != -1)
    cur_mv_line(i);
}

/* move to a new item relative to the current items on the screen */
static int
move_lines(int mv_lines)
{
  DBT *last_seen;
  const DBT *offset;
  int i, x, y, neg = 0;

  if (!mv_lines)
    return 0;

  if (mv_lines < 0) {
    neg = 1;
    mv_lines *= -1;
  }

  /* get current screen offset */
  if (neg) {
    /* find first non-empty item for current offset, if any */
    for (i = 0; i < keys.size && !keys.coll[i]; i++)
      continue;
  } else {
    /* find last non-empty item for current offset, if any */
    for (i = keys.size - 1; i > 0 && !keys.coll[i]; i--)
      continue;
  }

  if (i < keys.size)
    offset = keys.coll[i];
  else
    offset = NULL;

  /* if moving less than a screen down and the key is currently on the screen, move to it */
  if (!neg && mv_lines <= i && offset) {
    reload_scr(keys.coll[mv_lines]);
    return 0;
  }

  /* set iterator options */
  idx_itopts_t opts = {
    NULL, /* char *proj; */
    0, /* time_t minstart; */
    0, /* time_t maxstart; */
    0, /* int includemin; */
    0, /* int includemax; */
    1, /* size_t limit; */
    mv_lines - 1, /* size_t skip; */
    neg, /* int reverse; */
    (DBT *)offset /* DBT *offset; */
  };
  if (filter_enabled()) {
    if (proj_filter_active())
      opts.proj = gfilter.proj;
    opts.minstart = gfilter.start;
    opts.maxstart = gfilter.end;
  }

  last_seen = NULL;
  idx_iterate(&opts, fetch_nkey, &last_seen);

  log_warnx("%s: %u skip %zu, limit %zu, fetched: %u, neg: %d, offset: %s, last_seen: %s", __func__, mv_lines, opts.skip, opts.limit, nkeys.nextw, neg, idx_key_info(offset), idx_key_info(last_seen));

  /* save current position */
  getmaxyx(stdscr, y, x);

  reload_scr(last_seen);

  /* move cursor back */
  cur_mv_line(y);

  return 0;
}

/* move the viewport to the top no matter what */
static void
vp_mv_top(void)
{
  /* free all existing keys */
  free_keys(0);

  /* use a forward iterator without offset */
  move_lines(1);
}

/* move the viewport to the bottom no matter what */
static void
vp_mv_bottom(void)
{
  /* free all existing keys */
  free_keys(0);

  /* use a reverse iterator without offset */
  move_lines(-1 * e_lines);
}

/*
 * Fetch keys, and store in nkeys.
 *
 * Return 1 if more data is wanted, 0 if done, -1 on error.
 */
static int
fetch_nkey(DBT *key)
{
  nkeys.coll[nkeys.nextw++] = idx_copy_key(key);

  /* ready to get next entry if any */
  return 1;
}

/*
 * Print one line of project info, start and end time for the key at the given
 * index. If there is no key at the given index, print a blank line at the
 * corresponding row of the screen.
 *
 * Return 0 on succes, -1 on error.
 */
static int
print_key(int idx)
{
  const DBT *key = keys.coll[idx];
  FILE *pf;
  char line[MAXLINE];
  int linelen, i;
  int hours, minutes;
  char sdout[64], projcpy[11];

  if (key == NULL) {
    move(idx, 0);
    clrtoeol();
    return 0;
  }

  char *proj = idx_key_proj(key);
  time_t start = idx_key_start(key);
  time_t end = idx_key_end(key);

  if (strftime(sdout, sizeof sdout, "%a %e %b %Y %R", localtime(&start)) == 0)
    err(1, "%s: could not format broken-down start time", __func__);

  if (duration_in_hours(&start, &end, &hours, &minutes) == -1)
    errx(1, "%s: duration calculation error", __func__);

  // only print enough characters to fill up the screen
  if (vp_cols > sizeof line)
    linelen = sizeof line;
  else
    linelen = vp_cols - (10 + 3 + 20 + 3 + 5 + 3 + 1); // see printw format

  line[0] = '\0';
  if (linelen >= 4) {
    /* fetch the first line of the project file */
    if ((pf = idx_open_project_file(key)) == NULL)
      err(1, "%s: idx_open_project_file", __func__);

    if (fgets(line, sizeof line, pf) == NULL) {
      if (ferror(pf))
        err(1, "%s: fgets", __func__);
      else
        line[0] = '\0';
    }
    fclose(pf);

    // strip newline if any
    i = strcspn(line, "\n");
    line[i] = '\0';
    i--; // exclude null in length

    if (linelen < i)
      shorten(line, linelen);
  }

  if (strlcpy(projcpy, proj, sizeof projcpy) > sizeof projcpy) {
    projcpy[sizeof projcpy - 3] = '.';
    projcpy[sizeof projcpy - 2] = '.';
  }
  mvprintw(idx, 0, "%10s   %s   %2d:%02d   %s\n", projcpy, sdout, hours, minutes, line);

  // ready to get next entry if any
  return 1;
}

/*
 * calculate hours and minutes given the start and end time
 *
 * return 0 on success, -1 if there are any remaining seconds
 */
static int
duration_in_hours(const time_t *start, const time_t *end, int *hours, int *minutes)
{
  double span;

  *hours = 0;
  *minutes = 0;
  span = difftime(*end, *start);

  while (span >= 3600) {
    span -= 3600;
    (*hours)++;
  }

  while (span >= 60) {
    span -= 60;
    (*minutes)++;
  }

  if (span > 0)
    return -1;

  return 0;
}

/* force free all keys starting at i */
static void
free_keys(int i)
{
  while (i < keys.size) {
    idx_free_key(&keys.coll[i]);
    idx_free_key(&nkeys.coll[i]);
    keys.coll[i] = NULL;
    nkeys.coll[i] = NULL;
    i++;
  }
  nkeys.nextw = 0;
  clear();
}

/* return 0 on success, -1 on failure */
int
copy_file(const char *dataroot, const char *fname, FILE *src)
{
  int c, ret;
  FILE *fp;
  char pname[PATH_MAX];

  ret = 0;

  if (snprintf(pname, sizeof pname, "%s/%s", dataroot, fname) >= sizeof pname)
    errx(1, "%s: snprintf", __func__);

  if ((fp = fopen(pname, "w")) == NULL)
    err(1, "%s: fopen", __func__);

  while ((c = getc(src)) != EOF)
    if (putc(c, fp) == EOF)
      err(1, "%s: putc", __func__);

  if (ferror(src))
    ret = -1;

  if (fclose(fp) == EOF)
    err(1, "%s: fclose", __func__);

  return ret;
}
