# Title metadata and payload requirements for the engine release driver.
APP_NAME="SuperMarioWorld"
SUPPORTS_LUA=1
RELEASE_SLUG="SuperMarioWorldRecomp"        # matches the windows zip prefix
CMAKE_TARGET="SuperMarioWorldSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
REGEN_CMD="bash tools/regen.sh --stock"
PREBUILD_CMD=""
POSTBUILD_CMD=""
BOXART="recomp/launcher/boxart.tga"          # AppImage icon source (optional)
EXTRA_PAYLOAD=()                             # repo-relative files -> usr/bin/
# Release-owned mod catalog: packaging fails if the built tree is missing one of
# these rather than shipping an AppImage with an empty Mods page. Widescreen is
# a mod package (not a Display setting), so an AppImage without it would ship
# with no way to turn widescreen on at all.
REQUIRED_MOD_MANIFESTS=(
  "preloaded/packages/super-mario-world.enhancement.widescreen/1.0.0/manifest.toml"
)
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )

MOD_CATALOG_DIRS=(.)
MOD_CATALOG_DIRS=(preloaded)
configure_game() {
  case "$VARIANT" in
    stock) ;;
    coop)
      APP_NAME=SuperMarioWorldCoop; RELEASE_SLUG=SuperMarioWorldCoopSNESRecomp
      CMAKE_TARGET=SuperMarioWorldCoopSNESRecomp; REGEN_CMD="bash tools/regen.sh --coop"
      EXTRA_PAYLOAD=(recomp/coop/smw_coop.ips); FLAGS+=(-DSMW_BUILD_COOP=ON)
      [ -n "$BUILD" ] || BUILD="$REPO/build-linux-$CONFIG-coop" ;;
    *) echo "unsupported SMW variant: $VARIANT" >&2; exit 2 ;;
  esac
}
stage_game_payload() {
  if [ "$VARIANT" = stock ]; then
    [ -f "$BUILD/smw-falcon-cache" ] && [ -d "$BUILD/falcon-cache-licenses" ] || {
      echo "ERROR: Falcon cache helper or licenses missing" >&2; exit 1;
    }
    cp "$BUILD/smw-falcon-cache" "$APPDIR/usr/bin/"
    cp -r "$BUILD/falcon-cache-licenses" "$APPDIR/usr/bin/"
    chmod 0755 "$APPDIR/usr/bin/smw-falcon-cache"
  fi
  if [ "$ENABLE_LUA" = ON ]; then
    for name in 100_fireballs.lua README.md LICENSE-Lua.txt lua_tcp.py smw.lua; do
      [ -f "$(dirname "$BIN")/lua/$name" ] || { echo "ERROR: Lua payload missing $name" >&2; exit 1; }
    done
    cp -r "$(dirname "$BIN")/lua" "$APPDIR/usr/bin/lua"
  fi
}
