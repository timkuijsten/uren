#include "log.h"

void log_warn(const char *fmt, ...)
{
  va_list ap;

  if (!DEBUG)
    return;

  va_start(ap, fmt);
  vwarn(fmt, ap);
  va_end(ap);
}

void log_warnx(const char *fmt, ...)
{
  va_list ap;

  if (!DEBUG)
    return;

  va_start(ap, fmt);
  vwarnx(fmt, ap);
  va_end(ap);
}
