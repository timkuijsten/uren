#include "entryl.h"

FORM *create_str_form(const char *label, size_t inpsize, const char *def);
FORM *create_date_form(const char *label, const time_t def);
int destroy_form(FORM **form);
int spawn_editor(const char *pname);
int fetch(WINDOW *w, FORM *f, FIELD *complf, const char **compll);

/* Entry line form */

/*
 * create a form with a string input field, optionally with tab completion
 *
 * return 0 on success, -1 on failure, 1 if value should be ignored.
 */
int
entryl_str(WINDOW *w, char *dst, size_t dstsize, const char *label, const char *def, const char **tab_proj)
{
  FIELD *fp;
  FORM *form;
  WINDOW *sw;
  int ret;

  form = create_str_form(label, dstsize, def);

  if ((sw = derwin(w, 0, 0, 0, 0)) == NULL)
    errx(1, "%s: derwin", __func__);
  if (keypad(sw, TRUE) == ERR)
    errx(1, "%s: keypad", __func__);

  if (set_form_win(form, w) != E_OK)
    errx(1, "%s: set_form_win", __func__);
  if (set_form_sub(form, sw) != E_OK)
    errx(1, "%s: set_form_sub", __func__);
  if (post_form(form) != E_OK)
    errx(1, "%s: post_form", __func__);

  /* start at first input */
  if (form_driver(form, REQ_FIRST_FIELD) != E_OK)
    errx(1, "%s: form_driver", __func__);

  if ((fp = current_field(form)) == NULL)
    errx(1, "%s: current_field", __func__);

  ret = fetch(sw, form, fp, tab_proj);
  switch (ret) {
  case LERROR:
    ret = -1;
    break;
  case LCANCEL:
    ret = 1;
    break;
  case LSAVE:
    if (strlcpy(dst, field_buffer(fp, 0), dstsize) > dstsize)
      ret = -1;
    else
      ret = 0;
    break;
  default:
    ret = 0;
  }

  if (unpost_form(form) != E_OK)
    errx(1, "%s: unpost_form", __func__);
  destroy_form(&form);

  if (delwin(sw) == ERR)
    errx(1, "%s: delwin", __func__);

  return ret;
}

/*
 * create a form that consists of one delimited date entry
 *
 * return 0 on success, -1 on failure, 1 if value should be ignored.
 */
int
entryl_date(WINDOW *w, time_t *res, const char *label, time_t def)
{
  FORM *form;
  WINDOW *sw;
  int ret;

  form = create_date_form(label, def);

  if ((sw = derwin(w, 0, 0, 0, 0)) == NULL)
    errx(1, "%s: derwin", __func__);
  if (keypad(sw, TRUE) == ERR)
    errx(1, "%s: keypad", __func__);

  if (set_form_win(form, w) != E_OK)
    errx(1, "%s: set_form_win", __func__);
  if (set_form_sub(form, sw) != E_OK)
    errx(1, "%s: set_form_sub", __func__);
  if (post_form(form) != E_OK)
    errx(1, "%s: post_form", __func__);

  /* start at first input */
  if (form_driver(form, REQ_FIRST_FIELD) != E_OK)
    errx(1, "%s: form_driver", __func__);

  ret = fetch(sw, form, NULL, NULL);
  switch (ret) {
  case LERROR:
    ret = -1;
    break;
  case LCANCEL:
    ret = 1;
    break;
  case LSAVE:
    if (parse_date_field(form_fields(form), res) != 0)
      ret = -1;
    else
      ret = 0;
    break;
  default:
    ret = 0;
  }

  if (unpost_form(form) != E_OK)
    errx(1, "%s: unpost_form", __func__);
  destroy_form(&form);

  if (delwin(sw) == ERR)
    errx(1, "%s: delwin", __func__);

  return ret;
}

/*
 * display input form on a single line
 *
 * el: result is stored in here
 * line: where to display the input forms
 * proj: default name of the project
 * tab_proj: preload a list of all projects for TAB-complete
 * start: default start time
 * end: default end time
 * dataroot: data path for project files, NULL if editor should not be spawned
 * fname: file name for project files, NULL if editor should not be spawned
 * proj_opt: whether project is optional or not
 *
 * Return LSAVE, LCANCEL or LDELETE
 */
int
entryl(entryl_t *el, size_t line, const char *proj, const char **tab_proj, const time_t start, const time_t end, const char *dataroot, const char *fname, int proj_opt)
{
  WINDOW *w;
  char pname[PATH_MAX];
  int ret;

  ret = LSAVE;

  if ((w = newwin(1, 0, line, 0)) == NULL)
    errx(1, "%s: newwin", __func__);

  /* project */
  switch (entryl_str(w, el->proj, sizeof el->proj, "Project:", proj, tab_proj)) {
  case -1:
    errx(1, "%s: entryl_str", __func__);
  case 1:
    ret = LCANCEL;
    goto exit;
  }

  if (rtrim(el->proj) == 0) {
    if (proj_opt) {
      el->proj[0] = '\0'; /* erase trailing blanks */
    } else {
      info_prompt("Project may not be empty");
      ret = LERROR;
      goto exit;
    }
  }

  /* start date */
  switch (entryl_date(w, &el->start, "Start:", start)) {
  case -1:
    errx(1, "%s: entryl_date start", __func__);
  case 1:
    ret = LCANCEL;
    goto exit;
  }

  /* end date */
  switch (entryl_date(w, &el->end, "End:  ", max(end, el->start))) {
  case -1:
    errx(1, "%s: entryl_date end", __func__);
  case 1:
    ret = LCANCEL;
    goto exit;
  }

  if (el->start >= el->end) {
    info_prompt("0 minutes");
    ret = LERROR;
    goto exit;
  }

  if (dataroot == NULL || fname == NULL)
    goto exit;

  if (snprintf(pname, sizeof pname, "%s/%s", dataroot, fname) >= sizeof pname)
    errx(1, "%s: snprintf", __func__);

  /* description */
  switch(spawn_editor(pname)) {
  case -1:
    ret = LERROR;
    goto exit;
  case 1:
    ret = LCANCEL;
    goto exit;
  }

  if (strlcpy(el->fname, fname, sizeof el->fname) > sizeof el->fname)
    err(1, "%s: strlcpy fname", __func__);

exit:
  if (delwin(w) == ERR)
    errx(1, "%s: delwin", __func__);

  return ret;
}

/*
 * Create a string input form.
 *
 * Returns 0 on success, -1 on failure.
 */
FORM *
create_str_form(const char *label, size_t inpsize, const char *def)
{
  FORM *form;
  size_t llen; /* label length */
  FIELD **fields;

  llen = strlen(label);

  if ((fields = calloc(3, sizeof(FIELD *))) == NULL)
    err(1, "%s: calloc", __func__);

  fields[0] = new_field(1, llen, 0, 0, 0, 0);
  fields[1] = new_field(1, inpsize, 0, llen += 1, 0, 1); /* input */
  fields[2] = NULL;

  /* set labels */
  if (field_opts_off(fields[0], O_ACTIVE) != E_OK)
    errx(1, "%s: field_opts_off 0", __func__);

  if (set_field_back(fields[1], A_UNDERLINE) != E_OK)
    errx(1, "%s: set_field_back", __func__);

  if (set_field_buffer(fields[0], 0, label) != E_OK)
    errx(1, "%s: set_field_buffer label", __func__);

  if (def && set_field_buffer(fields[1], 0, def) != E_OK)
    errx(1, "%s: set_field_buffer def", __func__);

  if ((form = new_form(fields)) == NULL)
    err(1, "%s: new_form", __func__);

  return form;
}

/*
 * Create a date time input based on multiple fields.
 *
 * NOTE: closely related to parse_date_field.
 *
 * Returns 0 on success, -1 on failure.
 */
FORM *
create_date_form(const char *label, const time_t def)
{
  FORM *form;
  struct tm *bdt;
  char tmp[20];
  size_t llen; /* label length */
  FIELD **fields;

  llen = strlen(label);

  if ((fields = calloc(10, sizeof(FIELD *))) == NULL)
    err(1, "%s: calloc", __func__);

  fields[0] = new_field(1, llen, 0, 0, 0, 0);
  fields[1] = new_field(1, 2, 0, llen += 1, 0, 0); /* hour */
  fields[2] = new_field(1, 1, 0, llen += 2, 0, 0);
  fields[3] = new_field(1, 2, 0, llen += 1, 0, 0); /* minute */

  fields[4] = new_field(1, 2, 0, llen += 3, 0, 0); /* day */
  fields[5] = new_field(1, 1, 0, llen += 2, 0, 0),
  fields[6] = new_field(1, 2, 0, llen += 1, 0, 0), /* month */
  fields[7] = new_field(1, 1, 0, llen += 2, 0, 0);
  fields[8] = new_field(1, 4, 0, llen += 1, 0, 0); /* year */
  fields[9] = NULL;

  /* set ranges */
  set_field_type(fields[1], TYPE_INTEGER, 2, 0, 23);
  set_field_type(fields[3], TYPE_INTEGER, 2, 0, 59);
  set_field_type(fields[4], TYPE_INTEGER, 2, 1, 31);
  set_field_type(fields[6], TYPE_INTEGER, 2, 1, 12);
  set_field_type(fields[8], TYPE_INTEGER, 0, 1900, 9999);

  /* init fields */
  bdt = localtime(&def);

  if (strftime(tmp, sizeof tmp, "%H", bdt) == 0)
    errx(1, "%s: strftime H", __func__);
  set_field_buffer(fields[1], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%M", bdt) == 0)
    errx(1, "%s: strftime M", __func__);
  set_field_buffer(fields[3], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%d", bdt) == 0)
    errx(1, "%s: strftime d", __func__);
  set_field_buffer(fields[4], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%m", bdt) == 0)
    errx(1, "%s: strftime m", __func__);
  set_field_buffer(fields[6], 0, tmp);

  if (strftime(tmp, sizeof tmp, "%Y", bdt) == 0)
    errx(1, "%s: strftime Y", __func__);
  set_field_buffer(fields[8], 0, tmp);

  /* set labels */
  if (field_opts_off(fields[0], O_ACTIVE) != E_OK)
    errx(1, "%s: field_opts_off 0", __func__);
  if (field_opts_off(fields[2], O_ACTIVE) != E_OK)
    errx(1, "%s: field_opts_off 2", __func__);
  if (field_opts_off(fields[5], O_ACTIVE) != E_OK)
    errx(1, "%s: field_opts_off 5", __func__);
  if (field_opts_off(fields[7], O_ACTIVE) != E_OK)
    errx(1, "%s: field_opts_off 7", __func__);

  set_field_buffer(fields[0], 0, label);
  set_field_buffer(fields[2], 0, ":");
  set_field_buffer(fields[5], 0, "-");
  set_field_buffer(fields[7], 0, "-");

  if ((form = new_form(fields)) == NULL)
    err(1, "%s: new_form", __func__);

  return form;
}

/*
 * Free a date form and it's associated fields.
 *
 * Returns 0 on success, -1 on failure.
 */
int
destroy_form(FORM **form)
{
  int i;
  FIELD **fp;
  
  if ((fp = form_fields(*form)) == NULL)
    errx(1, "%s: form_fields", __func__);

  if (free_form(*form) != E_OK)
    errx(1, "%s: free_form", __func__);
  *form = NULL;

  i = 0;
  while (fp[i]) {
    if (free_field(fp[i]) != E_OK)
      errx(1, "%s: free_field %d", __func__, i);
    fp[i] = NULL;
    i++;
  }
  free(fp);

  return 0;
}

/*
 * f: the for
 * complf: field to TAB-complete
 * compll: list of completion options
 *
 * Return LSAVE if user wants to save the form, LCANCEL if the user wants to
 * cancel the form.
 */
int
fetch(WINDOW *w, FORM *form, FIELD *complf, const char **compll)
{
  char countstr[7]; /* null terminated storage of a count */
  int ret, proceed, key, prevkey;
  const char **complo = NULL; /* completion options */
  char *tabval;
  int comploi; /* currently selected option */

  proceed = 1;
  countstr[0] = '\0';

  /* save by default */
  ret = LSAVE;
  while (proceed && (key = wgetch(w)) != ERR) {
    switch (key) {
    case 1: /* ctrl-A */
      form_driver(form, REQ_BEG_LINE);
      break;
    case 5: /* ctrl-E */
      form_driver(form, REQ_END_LINE);
      break;
    case KEY_LEFT:
      form_driver(form, REQ_LEFT_CHAR);
      break;
    case KEY_RIGHT:
      form_driver(form, REQ_RIGHT_CHAR);
      break;
    case KEY_UP:
      /* fall through */
    case KEY_BTAB:
      form_driver(form, REQ_PREV_FIELD);
      break;
    case KEY_DOWN:
      /* fall through */
    case '\t':
      /* make sure buffer of current field is saved */
      if (form_driver(form, REQ_VALIDATION) != E_OK)
        errx(1, "%s: \\t save current buffer", __func__);

      /* if this field needs to be completed, show the next option */
      if (current_field(form) == complf) {
        if (prevkey != '\t') {
          /* 1. complo needs to be refilled and comploi initialized */
          tabval = strdup(field_buffer(complf, 0));
          rtrim(tabval);
          if (prefix_match(&complo, compll, tabval) != 0)
            errx(1, "%s: prefix_match", __func__);
          comploi = 0;

          /* set aside the user typed part */
          set_field_buffer(complf, 1, tabval);

          /* and fill with the first option */
          if (complo[comploi]) {
            set_field_buffer(complf, 0, complo[comploi++]);
            form_driver(form, REQ_END_LINE);
          }
          free(tabval);
        } else {
          /* 2. complo is already filled because the previous character was also a TAB */
          if (complo[comploi]) {
            set_field_buffer(complf, 0, complo[comploi++]);
            form_driver(form, REQ_END_LINE);
          } else { /* restore user typed part and reset comploi */
            set_field_buffer(complf, 0, field_buffer(complf, 1));
            form_driver(form, REQ_END_LINE);
            comploi = 0;
          }
        }
      } else {
        form_driver(form, REQ_NEXT_FIELD);
      }
      break;
    case KEY_DC:
      form_driver(form, REQ_DEL_CHAR);
      break;
    case 127: /* backspace on OS X */
      /* fall through */
    case KEY_BACKSPACE:
      form_driver(form, REQ_DEL_PREV);
      break;
    case 27: /* ESC */
      /* don't save and exit */
      ret = LCANCEL;
      proceed = 0;
      break;
    case '\n':
      /* save and exit */
      /* make sure buffer of current field is saved */
      if (form_driver(form, REQ_VALIDATION) != E_OK) {
        info_prompt("illegal value");
        ret = LERROR;
      }
      proceed = 0;
      break;
    default:
      /* if this field needs to be completed, suggest a completion if any */
      form_driver(form, key);
    }
    prevkey = key;
  }

  free(complo);
  return ret;
}

/*
 * Spawns the users editor and updates the buffer with the result of the editor.
 *
 * fp contains an open file descriptor on success.
 *
 * Return 0 on success, -1 on error, 1 if user file is to be ignored.
 */
int
spawn_editor(const char *pname)
{
  struct stat s;
  pid_t pid;
  int status;
  char *editor;

  /* get editor from the user or default to vi */
  if ((editor = getenv("EDITOR")) == NULL)
    editor = "vi";

  if ((pid = fork()) < 0) {
    err(1, "%s: fork", __func__);
  } else if (pid == 0) {
    execlp(editor, editor, pname, (char *)NULL);
    err(1, "%s: execlp", __func__);
  } else {
    if (wait(&status) < 0)
      err(1, "%s: wait", __func__);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      log_warnx("%s: editor \"%s\" no clean exit", __func__, editor);
      return -1;
    }

    if (stat(pname, &s) == -1) {
      if (errno == ENOENT)
        return 1;
      else
        return -1;
    }
  }

  return 0;
}
