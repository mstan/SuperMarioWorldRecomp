#ifndef SMW_MOD_CATALOG_H
#define SMW_MOD_CATALOG_H

#include <string.h>
#include "mod_runtime.h"
#if defined(RECOMP_LAUNCHER)
#include "recomp_launcher.h"
#endif

/* Check availability, not enablement: co-op remains the player's choice.
 * The shared runtime accepts an empty catalog, but this application bundles
 * co-op. A partial extraction must not silently turn it into ordinary SMW. */
static inline const char *SmwInitializeModCatalog(const char *root) {
  if (!snes_mod_runtime_initialize_c(root, "super-mario-world-us",
      "0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b"))
    return "The mod files could not be loaded.\n\n"
           "Extract the complete download into a new folder and try again. "
           "Keep your existing saves and settings.";
#if defined(RECOMP_LAUNCHER)
  const RecompLauncherCModProvider *provider =
      snes_mod_runtime_launcher_provider_c();
  if (provider && provider->feature_count && provider->feature_get) {
    int count = provider->feature_count(provider->ctx);
    for (int i = 0; i < count; ++i) {
      RecompLauncherCModFeature feature;
      memset(&feature, 0, sizeof(feature));
      if (provider->feature_get(provider->ctx, i, &feature) &&
          strcmp(feature.package_id,
                 "super-mario-world.gameplay.native-coop") == 0 &&
          strcmp(feature.id, "simultaneous") == 0)
        return NULL;
    }
  }
  return "The co-op files are missing from this installation.\n\n"
         "Extract the complete download into a new folder and try again. "
         "Keep your existing saves and settings.";
#else
  return NULL;
#endif
}

#endif
