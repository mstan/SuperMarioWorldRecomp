#include "mod_runtime.h"
#include "mods/coop/coop_runtime.h"
static void reset_coop(void) {SmwCoopEnable(false);}
static void activate_coop(void) {SmwCoopEnable(true);}
SNES_MOD_CONSTRUCTOR(smw_register_native_coop_plugin) {
    (void)snes_mod_register_reset_callback(reset_coop);
    (void)snes_mod_register_activation_plugin("super-mario-world.native-coop",activate_coop);
}
