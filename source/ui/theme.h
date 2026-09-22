/* theme.h - selects the active color palette from themes/. */

#ifndef LIZ_THEME_H
#define LIZ_THEME_H

#include "config.h"

#ifdef LIZ_THEME_THUNAR
#include "ui/themes/thunar.h"
#endif

#ifdef LIZ_THEME_CATPPUCCIN_MOCHA
#include "ui/themes/catppuccin-mocha.h"
#endif

#ifdef LIZ_THEME_AYU_DARK
#include "ui/themes/ayu-dark.h"
#endif

#ifdef LIZ_THEME_NORD
#include "ui/themes/nord.h"
#endif

#ifdef LIZ_THEME_TOKYO_DARK
#include "ui/themes/tokyodark.h"
#endif

#ifdef LIZ_THEME_EVERFOREST_DARK
#include "ui/themes/everforest-dark.h"
#endif

#ifdef LIZ_THEME_DEFAULT
#include "ui/themes/default.h"
#endif

#endif /* LIZ_THEME_H */
