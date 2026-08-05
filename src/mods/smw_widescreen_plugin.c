#include "mod_runtime.h"
#include "recomp_launcher.h"
#include "config.h"

#include <string.h>

/*
 * Widescreen moved out of the launcher's Display settings and into the mod
 * package catalog, matching Mega Man X / X2 / Super Mario Kart. The renderer,
 * HUD split, and the generated object-lifecycle override layer all stay where
 * they are — this plugin only owns the player-facing activation and its two
 * options.
 *
 * The reset callback runs before active plugins on every launch, so a disabled
 * feature deterministically restores stock 4:3 even if config.ini still carries
 * a widescreen value written by an older build.
 */

#define SMW_WS_PACKAGE "super-mario-world.enhancement.widescreen"
#define SMW_WS_FEATURE "widescreen"

static void smw_widescreen_reset(void) {
  g_config.widescreen_mode = kWidescreenMode_Standard;
  g_config.widescreen_hud = false;
}

static void smw_widescreen_activate(void) {
  /* Defaults for an enabled feature whose options cannot be read: match the
   * manifest defaults (adaptive + split HUD) rather than silently falling back
   * to 4:3, which would make the Mods toggle look broken. */
  g_config.widescreen_mode = kWidescreenMode_Adaptive;
  g_config.widescreen_hud = true;

  const RecompLauncherCModProvider *provider =
      snes_mod_runtime_launcher_provider_c();
  if (!provider || !provider->feature_option_get)
    return;

  RecompLauncherCModOption option;
  for (int i = 0; i < 16; i++) {
    memset(&option, 0, sizeof(option));
    if (!provider->feature_option_get(provider->ctx, SMW_WS_PACKAGE,
                                      SMW_WS_FEATURE, i, &option))
      break;
    if (strcmp(option.id, "mode") == 0) {
      g_config.widescreen_mode =
          (strcmp(option.value, "16_9") == 0) ? kWidescreenMode_Fixed16x9
                                              : kWidescreenMode_Adaptive;
    } else if (strcmp(option.id, "hud") == 0) {
      g_config.widescreen_hud = (strcmp(option.value, "centered") != 0);
    }
  }
}

SNES_MOD_CONSTRUCTOR(smw_register_widescreen_plugin) {
  (void)snes_mod_register_reset_callback(smw_widescreen_reset);
  (void)snes_mod_register_activation_plugin("super-mario-world.widescreen",
                                            smw_widescreen_activate);
}
