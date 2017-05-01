#include "shared.h"

/*
 * Right trim a field. s must be NUL terminated.
 *
 * Return the new string length of s including the nul byte.
 */
int
rtrim(char *s)
{
  ssize_t i;

  i = strlen(s);
  while (i && isspace(s[i - 1]))
    i--;
  s[i] = '\0';

  return i;
}

/*
 * Expect countstr to be null terminated. If countstr > 0 it sets count and
 * resets countstr to the empty string.
 *
 * Return 0 on update, 1 if both count and countstr are not changed and -1 on
 * log_error.
 */
int
use_count(size_t *count, char *countstr)
{
  if (strlen(countstr)) {
    if ((*count = strtoimax(countstr, NULL, 10)) == 0)
      log_err(1, "%s: strtoimax", __func__);
    countstr[0] = '\0';
    return 0;
  }

  return 1;
}

/*
 * sets the first 9 fields in field to a date type
 *
 * NOTE: closely related to parse_date_field.
 *
 * Returns a pointer to the last field that is added.
 */
FIELD **
set_date_field(FIELD **field, const char *label, const int col, const int row, const time_t def)
{
  struct tm *bdt;
  char tmp[20];
  size_t llen; /* label length */

  llen = strlen(label);

  field[0] = new_field(1, llen, row, col, 0, 0);
  field[1] = new_field(1, 2, row, col + llen + 2, 0, 0); /* hour */
  field[2] = new_field(1, 1, row, col + llen + 4, 0, 0);
  field[3] = new_field(1, 2, row, col + llen + 5, 0, 0); /* minute */
  field[4] = new_field(1, 2, row, col + llen + 8, 0, 0); /* day */
  field[5] = new_field(1, 1, row, col + llen + 10, 0, 0);
  field[6] = new_field(1, 2, row, col + llen + 11, 0, 0); /* month */
  field[7] = new_field(1, 1, row, col + llen + 13, 0, 0);
  field[8] = new_field(1, 4, row, col + llen + 14, 0, 0); /* year */

  /* set ranges */
  set_field_type(field[1], TYPE_INTEGER, 2, 0, 23);
  set_field_type(field[3], TYPE_INTEGER, 2, 0, 59);
  set_field_type(field[4], TYPE_INTEGER, 2, 1, 31);
  set_field_type(field[6], TYPE_INTEGER, 2, 1, 12);
  set_field_type(field[8], TYPE_INTEGER, 0, 2000, 9999);

  /* init fields */
  bdt = localtime(&def);

  if (strftime(tmp, sizeof tmp, "%H", bdt) == 0)
    log_errx(1, "%s: strftime H", __func__);
  set_field_buffer(field[1], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%M", bdt) == 0)
    log_errx(1, "%s: strftime M", __func__);
  set_field_buffer(field[3], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%d", bdt) == 0)
    log_errx(1, "%s: strftime d", __func__);
  set_field_buffer(field[4], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%m", bdt) == 0)
    log_errx(1, "%s: strftime m", __func__);
  set_field_buffer(field[6], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%Y", bdt) == 0)
    log_errx(1, "%s: strftime Y", __func__);
  set_field_buffer(field[8], 0, tmp);

  /* set labels */
  if (field_opts_off(field[0], O_ACTIVE) != E_OK)
    log_errx(1, "%s: field_opts_off 0", __func__);
  if (field_opts_off(field[2], O_ACTIVE) != E_OK)
    log_errx(1, "%s: field_opts_off 2", __func__);
  if (field_opts_off(field[5], O_ACTIVE) != E_OK)
    log_errx(1, "%s: field_opts_off 5", __func__);
  if (field_opts_off(field[7], O_ACTIVE) != E_OK)
    log_errx(1, "%s: field_opts_off 7", __func__);

  set_field_buffer(field[0], 0, label);
  set_field_buffer(field[2], 0, ":");
  set_field_buffer(field[5], 0, "-");
  set_field_buffer(field[7], 0, "-");

  return field + 8;
}

/*
 * Read the first 9 fields in field as %H %M %d %m %Y format with labels in
 * between.
 *
 * NOTE: closely related to set_date_field.
 *
 * field is a form field, result is stored in t.
 *
 * Return 0 on success, -1 on error.
 */
int
parse_date_field(FIELD **field, time_t *t)
{
  int i = 0;
  struct tm dt;
  char *buf, timestr[12];

  if ((buf = field_buffer(field[1], 0)) == NULL) /* hour */
    log_err(1, "%s: field_buffer 0 hour", __func__);
  timestr[i++] = buf[0];
  timestr[i++] = buf[1];

  if ((buf = field_buffer(field[3], 0)) == NULL) /* minute */
    log_err(1, "%s: field_buffer 1 minute", __func__);
  timestr[i++] = buf[0];
  timestr[i++] = buf[1];

  if ((buf = field_buffer(field[4], 0)) == NULL) /* day */
    log_err(1, "%s: field_buffer 2 day", __func__);
  timestr[i++] = buf[0];
  timestr[i++] = buf[1];

  if ((buf = field_buffer(field[6], 0)) == NULL) /* month */
    log_err(1, "%s: field_buffer 3 month", __func__);
  timestr[i++] = buf[0];
  timestr[i++] = buf[1];

  if ((buf = field_buffer(field[8], 0)) == NULL) /* year */
    log_err(1, "%s: field_buffer 4 year", __func__);
  timestr[i++] = buf[0];
  timestr[i++] = buf[1];
  timestr[i++] = buf[2];
  timestr[i++] = buf[3];

  /* determine calendar time */
  if (strptime(timestr, "%H%M%d%m%Y", &dt) == NULL) {
    log_warnx("%s: could not parse calendar time from: %s", __func__, timestr);
    return -1;
  }

  /* clear values that are not in the above time spec */
  dt.tm_sec = 0;
  dt.tm_isdst = -1;

  *t = mktime(&dt);

  return 0;
}

/*
 * Show info prompt at the bottom of the screen.
 */
void
info_prompt(const char *msg)
{
  int i, j, x, y;

  /* save current position */
  getyx(stdscr, i, j);

  getmaxyx(stdscr, y, x);
  mvprintw(y - 1, 0, msg);
  clrtoeol();

  /* restore cursor */
  move(i, j);

  refresh();
  sleep(1);
}
