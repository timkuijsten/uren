#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat/compat.h"

int prefix_match(const char ***dst, const char **src, const char *prefix);
int common_prefix(const char **av);
