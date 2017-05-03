#include "uren.h"

static char progname[MAXPROG];
static void usage(void);
static user_t user;
static int init_user(user_t *usr);

int
main(int argc, char *argv[])
{
  char datapath[PATH_MAX + 1];
  char idxpath[PATH_MAX + 1];

  if (isatty(STDIN_FILENO) == 0)
    log_err(1, "%s: stdin is not connected to a terminal", __func__);

#ifdef VDSUSP
  struct termios term;

  /* enable ^Y */
  if (tcgetattr(STDIN_FILENO, &term) < 0)
    log_err(1, "%s: tcgetattr", __func__);
  term.c_cc[VDSUSP] = _POSIX_VDISABLE;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) < 0)
    log_err(1, "%s: tcsetattr", __func__);
#endif

  /* make sure MB_CUR_MAX is set */
  if (setlocale(LC_ALL, "") == NULL)
    log_err(1, "%s: setlocale", __func__);

  /* check if the current locale uses a state-dependent encoding (not UTF-8, C or POSIX) */
  if (mblen(NULL, MB_CUR_MAX) != 0)
    log_warnx("%s: state-dependent encoding used", __func__);

  if (strlcpy(progname, basename(argv[0]), MAXPROG) > MAXPROG)
    log_errx(1, "%s: program name too long", __func__);

  if (argc == 2)
    usage();

  if (init_user(&user) < 0)
    log_errx(1, "%s: can't initialize user", __func__);

  /* setup datapath */
  if (strlcpy(datapath, user.home, PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(datapath, "/", PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(datapath, DATADIR, PATH_MAX) >= PATH_MAX)
    return -1;

  /* setup index path */
  if (strlcpy(idxpath, datapath, PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(idxpath, "/", PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(idxpath, IDXPATH, PATH_MAX) >= PATH_MAX)
    return -1;

  /* ensure index */
  if (idx_open(datapath, idxpath, 0) == -1)
    log_errx(1, "%s: can't initialize indices", __func__);
  if (atexit(idx_close) != 0)
    log_errx(1, "%s: can't register idx_close", __func__);

  vp_init(datapath);
  return vp_start();
}

static void
usage(void)
{
  printf("usage: %s [-h] [project]\n", progname);
  exit(0);
}

/* set username and home dir
 * return 0 on success or -1 on failure.
 */
static int
init_user(user_t *usr)
{
  struct passwd *pw;

  if ((pw = getpwuid(getuid())) == NULL)
    return -1; /* user not found */
  if (strlcpy(usr->name, pw->pw_name, MAXUSER) >= MAXUSER)
    return -1; /* username truncated */
  if (strlcpy(usr->home, pw->pw_dir, PATH_MAX) >= PATH_MAX)
    return -1; /* home dir truncated */

  return 0;
}
