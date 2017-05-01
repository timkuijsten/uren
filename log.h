#include <err.h>
#include <stdarg.h>

#define DEBUG 0

void log_warn(const char *fmt, ...);
void log_warnx(const char *fmt, ...);
void log_err(int eval, const char *fmt, ...);
void log_errx(int eval, const char *fmt, ...);
