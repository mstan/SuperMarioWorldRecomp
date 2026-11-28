// Exercise the real package scanner, saved selections and co-op activation.
#include "smw_mod_catalog.h"
#include "content_variant.h"
extern "C" {
#include "mods/coop/coop_runtime.h"
}
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
// No fixture declares a content variant; program switching is outside this test.
extern "C" void snes_variant_clear_declared(void) {}
extern "C" const SnesContentVariant *snes_variant_register_declared(const SnesVariantDecl *) {
  return nullptr;
}
static const char *package_id = "super-mario-world.gameplay.native-coop";

int main(int argc, char **argv) {
  assert(argc == 3); // Isolated output directory and source manifest.
  const fs::path out = fs::absolute(argv[1]);
  const fs::path manifest = fs::absolute(argv[2]);
  fs::create_directories(out);
  const auto root = out / "mods/preloaded";
  const auto coop = root / "packages" / package_id / "0.1.0/manifest.toml";

  // The engine accepts an empty catalog. SMW must report the missing bundle.
  assert(snes_mod_runtime_initialize_c(root.u8string().c_str(), "super-mario-world-us",
      "0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b"));
  const char *error = SmwInitializeModCatalog(root.u8string().c_str());
  assert(error && std::strstr(error, "co-op files are missing"));
  assert(!SmwCoopEnabled());

  // Selecting a ROM elsewhere or putting it beside the application first
  // cannot change package discovery. These are synthetic, non-game bytes.
  for (const char *location : {"elsewhere", "installation"}) {
    fs::create_directories(out / location);
    std::ofstream(out / location / "smw.sfc") << "synthetic image";
    error = SmwInitializeModCatalog(root.u8string().c_str());
    assert(error && std::strstr(error, "co-op files are missing"));
  }

  fs::create_directories(coop.parent_path());
  fs::copy_file(manifest, coop);
  assert(!SmwInitializeModCatalog(root.u8string().c_str()));
  assert(!snes_mod_runtime_feature_enabled_c(package_id, "simultaneous"));
  assert(snes_mod_runtime_commit_c(nullptr));
  snes_mod_runtime_activate_plugins_c();
  assert(!SmwCoopEnabled()); // Availability never forces an elective mod on.

  const auto *provider = snes_mod_runtime_launcher_provider_c();
  assert(provider && provider->feature_enable(provider->ctx, package_id, "simultaneous", 1));
  assert(snes_mod_runtime_commit_c(nullptr));
  snes_mod_runtime_activate_plugins_c();
  assert(SmwCoopEnabled());
  assert(!SmwInitializeModCatalog(root.u8string().c_str()));
  assert(snes_mod_runtime_feature_enabled_c(package_id, "simultaneous"));
  assert(snes_mod_runtime_commit_c(nullptr));
  snes_mod_runtime_activate_plugins_c();
  assert(SmwCoopEnabled()); // Saved enabled choice survives the startup check.

  const auto state_path = root / "state.toml";
  auto read_state = [&]() {
    std::ifstream file(state_path);
    return std::string(std::istreambuf_iterator<char>(file), {});
  };
  const auto saved = read_state();
  // A different bundled package is not evidence that co-op was extracted.
  const auto other_id = "super-mario-world.enhancement.widescreen";
  const auto other = root / "packages" / other_id / "1.0.0/manifest.toml";
  fs::create_directories(other.parent_path());
  fs::copy_file(manifest.parent_path().parent_path().parent_path() /
                other_id / "1.0.0/manifest.toml", other);
  fs::remove(coop); // Partial extraction, with the player's choice still saved.
  error = SmwInitializeModCatalog(root.u8string().c_str());
  assert(error && std::strstr(error, "co-op files are missing"));
  assert(read_state() == saved);
  fs::remove(other);
  fs::copy_file(manifest, coop);
  assert(!SmwInitializeModCatalog(root.u8string().c_str()));
  assert(snes_mod_runtime_feature_enabled_c(package_id, "simultaneous"));
  assert(read_state() == saved);

  std::ofstream(coop) << "invalid manifest";
  error = SmwInitializeModCatalog(root.u8string().c_str());
  assert(error && std::strstr(error, "could not be loaded"));
  assert(read_state() == saved);
  std::ofstream(out / "blocked") << "not a directory";
  error = SmwInitializeModCatalog((out / "blocked").u8string().c_str());
  assert(error && std::strstr(error, "could not be loaded"));
  std::cout << "mod catalog: empty, partial, malformed, inaccessible, ROM locations, "
               "saved choices and real co-op activation passed\n";
}
