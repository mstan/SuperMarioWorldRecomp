#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Native dialogs are independent of SDL's dummy video backend. */
static inline bool SmwUiDialogsAllowed(void) {
  const char *disabled = getenv("SNESRECOMP_NO_DIALOGS");
  const char *no_launcher = getenv("SNESRECOMP_NO_LAUNCHER");
  const char *driver = getenv("SDL_VIDEO_DRIVER");
  if (!driver || !driver[0]) driver = getenv("SDL_VIDEODRIVER");
  return !(disabled && disabled[0] && strcmp(disabled, "0")) &&
      !(no_launcher && no_launcher[0]) &&
      !(driver && (!strcmp(driver, "dummy") || !strcmp(driver, "offscreen")));
}
