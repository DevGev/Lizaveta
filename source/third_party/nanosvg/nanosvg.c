/* Translation unit that instantiates the vendored nanosvg headers.
 * Both are header-only: exactly one .c must define the IMPLEMENTATION
 * macros, everyone else just includes the headers. */

#include <stdio.h>
#include <string.h>
#include <math.h>

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvg/nanosvgrast.h"
