#include "mod_runtime.h"
#include "smw_replay.h"

static void reset_diagnostics(void) { SmwReplayEnable(false); }
static void activate_diagnostics(void) { SmwReplayEnable(true); }
SNES_MOD_CONSTRUCTOR(smw_register_diagnostics_plugin) {
  (void)snes_mod_register_reset_callback(reset_diagnostics);
  (void)snes_mod_register_activation_plugin(
      "super-mario-world.diagnostics", activate_diagnostics);
}
