#include "mods/falcon/captain_falcon_foreign.h"
#include "mods/falcon/smw_falcon_presentation_runtime.h"

#include "foreign_controller.h"
#include "mod_runtime.h"
#include "desktop/sdl_compat.h"
#include "snes/interp_bridge.h"
#include "mods/smw_falcon_plugin.h"
#include <string.h>

#define SMW_FALCON_PLUGIN "super-mario-world.smash64.captain-falcon"
static int s_enabled;

const char *snes_mod_external_rom_path(const char *package_id,
                                       const char *feature_id,
                                       const char *resource_id);

static void smw_falcon_reset(void)
{
    s_enabled = 0;
    interp_bridge_set_scheduler_aot_policy(-1);
    smw_falcon_presentation_reset();
    snes_foreign_select(NULL);
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
}

static void smw_falcon_activate(void)
{
    const char *owner_rom = snes_mod_external_rom_path(
        SMW_FALCON_PLUGIN, "captain-falcon", "smash64-us-v10");
    if (!smw_falcon_presentation_activate(owner_rom)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Captain Falcon could not load",
            "Falcon's assets could not be prepared. Select your Super Smash Bros. "
            "US v1.0 ROM in Mods and make sure the complete release is extracted "
            "and your user cache folder is writable. The game will use Mario.", NULL);
        return;
    }
    if (snes_foreign_select(SMW_CAPTAIN_FALCON_ID)) {
        s_enabled = 1;
        /* Falcon's collision/input hooks are installed in generated bodies.
         * The stock LLE-only scheduler never executes those bodies. */
        interp_bridge_set_scheduler_aot_policy(1);
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
    }
}

void smw_falcon_restore_selection(int restored_foreign_state)
{
    const ForeignController *active = snes_foreign_active();
    if (!s_enabled) {
        snes_foreign_select(NULL);
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
    } else if (!restored_foreign_state || !active ||
               strcmp(active->id, SMW_CAPTAIN_FALCON_ID) != 0) {
        snes_foreign_select(SMW_CAPTAIN_FALCON_ID);
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
    }
}

SNES_MOD_CONSTRUCTOR(smw_register_falcon_plugin)
{
    (void)smw_captain_falcon_register();
    (void)snes_mod_register_reset_callback(smw_falcon_reset);
    (void)snes_mod_register_activation_plugin(SMW_FALCON_PLUGIN,
                                              smw_falcon_activate);
}
