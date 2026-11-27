#!/usr/bin/env bash
# build-macos.sh — macOS build/package script for SuperMarioWorldRecomp.
#
# macOS counterpart to tools/build-linux.sh and tools/make_release.ps1, with the
# same prod-vs-debug discipline (prod strips the TCP debug server + rings).
#
# Run this ON a Mac (it cannot be built on Linux/Windows). See docs/macos.md.
# It configures + builds with clang, then bundles a relocatable
# <APP_NAME>.app (dylibbundler copies SDL in and rewrites install names when
# SDL is a shared library) and a <APP_NAME>.dmg for distribution.
#
# Usage:
#   bash tools/build-macos.sh                  # prod .app + .dmg (default)
#   bash tools/build-macos.sh --config debug   # debug build (TCP server + rings)
#   bash tools/build-macos.sh --regen          # regen src/gen first (needs cargo)
#   bash tools/build-macos.sh --no-dmg         # .app only
#   bash tools/build-macos.sh --arch arm64|x86_64|universal   # default: host arch
#
# Prereqs: Xcode Command Line Tools, Python 3, and (Homebrew) cmake + sdl3.
#   brew install cmake sdl3 dylibbundler        # create-dmg optional
# Without a system SDL3 the build fetches and compiles the pinned SDL3.
#
# User state (config.ini, keybinds.ini, rom.cfg, saves/, logs/, mods/) lives in
#   ~/Library/Application Support/SuperMarioWorld
# never inside the .app (see the launcher script written below).
set -euo pipefail

# ============================ PER-GAME CONFIG ===============================
APP_NAME="SuperMarioWorld"
DISPLAY_NAME="Super Mario World"
CMAKE_TARGET="SuperMarioWorldSNESRecomp"
ROM_EXTS="sfc smc"
REGEN_CMD="bash tools/regen.sh --stock"
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )
BUNDLE_ID="com.mstan.supermarioworldrecomp"
# ============================================================================

CONFIG="prod"; DO_REGEN=0; DO_DMG=1
ARCH="$(uname -m)"   # arm64 on Apple Silicon, x86_64 on Intel
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/release-macos"
VERSION="$(sed -n 's/^set(SNESRECOMP_BUILD_VERSION "\([^"]*\)".*/\1/p' "$REPO/CMakeLists.txt" | head -1)"
VERSION="${VERSION:-0.0.0}"

while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2;;
    --prod) CONFIG="prod"; shift;;
    --debug) CONFIG="debug"; shift;;
    --regen) DO_REGEN=1; shift;;
    --no-dmg) DO_DMG=0; shift;;
    --arch) ARCH="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    -h|--help) sed -n '2,25p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
case "$CONFIG" in prod) FLAGS=( "${PROD_CMAKE_FLAGS[@]}" );; debug) FLAGS=( "${DEBUG_CMAKE_FLAGS[@]}" );;
  *) echo "--config must be prod or debug" >&2; exit 2;; esac
[ "$(uname -s)" = "Darwin" ] || { echo "ERROR: run this on macOS." >&2; exit 1; }

case "$ARCH" in
  universal) OSX_ARCHS="x86_64;arm64";;
  arm64|x86_64) OSX_ARCHS="$ARCH";;
  *) echo "--arch must be arm64, x86_64, or universal" >&2; exit 2;;
esac
FLAGS+=( -DSNESRECOMP_SDL_BACKEND="${SNESRECOMP_SDL_BACKEND:-SDL3}" )

BUILD="$REPO/build-macos-$CONFIG"
echo "==================== $APP_NAME $VERSION ($CONFIG, $ARCH) ===================="
cd "$REPO"

# Engine-side macOS changes live as a patch so the snesrecomp submodule keeps
# pointing at the upstream pin. Apply once; skip when already applied.
ENGINE_PATCH="$REPO/macos/patches/snesrecomp-macos-data-dir.patch"
if [ -f "$ENGINE_PATCH" ]; then
  if git -C "$REPO/snesrecomp" apply --reverse --check "$ENGINE_PATCH" >/dev/null 2>&1; then
    echo "[engine] macOS data-dir patch already applied"
  elif git -C "$REPO/snesrecomp" apply --check "$ENGINE_PATCH" >/dev/null 2>&1; then
    git -C "$REPO/snesrecomp" apply "$ENGINE_PATCH"
    echo "[engine] applied macOS data-dir patch to snesrecomp"
  else
    echo "ERROR: $ENGINE_PATCH does not apply to this snesrecomp checkout." >&2
    exit 1
  fi
fi

[ "$DO_REGEN" = "1" ] && { echo "[regen] $REGEN_CMD"; eval "$REGEN_CMD"; }
ls src/gen/*.c >/dev/null 2>&1 || {
  echo "ERROR: src/gen/ is empty. Put your smw.sfc at the repo root and rerun with --regen." >&2
  exit 1
}

# The Captain Falcon mod needs a PyInstaller helper. Bundle it only when the
# build Python can produce it; the rest of the game does not depend on it.
PY="${PYTHON:-$(command -v python3)}"
if [ -z "${SMW_BUNDLE_FALCON_CACHE_HELPER:-}" ]; then
  if "$PY" -c "import PIL, PyInstaller" >/dev/null 2>&1; then
    SMW_BUNDLE_FALCON_CACHE_HELPER=ON
  else
    SMW_BUNDLE_FALCON_CACHE_HELPER=OFF
    echo "      note: PIL/PyInstaller absent -> Captain Falcon helper not bundled"
  fi
fi
FLAGS+=( -DSMW_BUNDLE_FALCON_CACHE_HELPER="$SMW_BUNDLE_FALCON_CACHE_HELPER"
         -DPython3_EXECUTABLE="$PY" )

echo "[1/4] configure"
cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}" \
    "${FLAGS[@]}"
echo "[2/4] build ($CMAKE_TARGET)"
cmake --build "$BUILD" --target "$CMAKE_TARGET" -j"$(sysctl -n hw.ncpu)"

BIN="$(find "$BUILD" -maxdepth 3 -type f -name "$CMAKE_TARGET" -perm +111 | head -1)"
[ -n "$BIN" ] || { echo "ERROR: no binary named '$CMAKE_TARGET' under $BUILD" >&2; exit 1; }
BINDIR="$(dirname "$BIN")"
echo "      bin: $BIN"

echo "[3/4] bundle $APP_NAME.app"
mkdir -p "$OUT"
APPDIR="$OUT/$APP_NAME.app"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/Contents/MacOS" "$APPDIR/Contents/Resources" "$APPDIR/Contents/Frameworks"
cp "$BIN" "$APPDIR/Contents/MacOS/$CMAKE_TARGET"

# Read-only payload lives in Resources. SDL_GetBasePath() resolves there inside
# a bundle (shaders), and the launcher script exposes it to the data dir.
[ -d "$BINDIR/assets" ] || { echo "ERROR: launcher assets/ missing beside $BIN" >&2; exit 1; }
cp -R "$BINDIR/assets" "$APPDIR/Contents/Resources/assets"
[ -d "$BINDIR/mods/preloaded" ] || {
  echo "ERROR: built-in mod catalog missing: $BINDIR/mods/preloaded" >&2; exit 1; }
mkdir -p "$APPDIR/Contents/Resources/mods"
cp -R "$BINDIR/mods/preloaded" "$APPDIR/Contents/Resources/mods/preloaded"
if [ -f "$BUILD/smw-falcon-cache" ]; then
  cp "$BUILD/smw-falcon-cache" "$APPDIR/Contents/MacOS/smw-falcon-cache"
  chmod 0755 "$APPDIR/Contents/MacOS/smw-falcon-cache"
  [ -d "$BUILD/falcon-cache-licenses" ] && \
    cp -R "$BUILD/falcon-cache-licenses" "$APPDIR/Contents/Resources/falcon-cache-licenses"
fi

# App icon from the launcher box art (best effort; sips/iconutil ship with macOS).
ICON_KEY=""
if [ -f "$REPO/recomp/launcher/boxart.tga" ] && command -v iconutil >/dev/null 2>&1; then
  ICONSET="$BUILD/AppIcon.iconset"
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  SRC_PNG="$BUILD/boxart.png"
  if sips -s format png "$REPO/recomp/launcher/boxart.tga" --out "$SRC_PNG" >/dev/null 2>&1; then
    sips -p 1024 1024 --padColor 000000 "$SRC_PNG" --out "$BUILD/icon1024.png" >/dev/null 2>&1 || \
      cp "$SRC_PNG" "$BUILD/icon1024.png"
    for sz in 16 32 128 256 512; do
      sips -z $sz $sz "$BUILD/icon1024.png" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null 2>&1
      sips -z $((sz*2)) $((sz*2)) "$BUILD/icon1024.png" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null 2>&1
    done
    if iconutil -c icns "$ICONSET" -o "$APPDIR/Contents/Resources/AppIcon.icns" 2>/dev/null; then
      ICON_KEY="<key>CFBundleIconFile</key><string>AppIcon</string>"
    fi
  fi
fi

# Launcher. User state (config.ini, keybinds.ini, rom.cfg, saves/, logs/, mods)
# goes to ~/Library/Application Support/<game>: host_paths.c and keybinds.c
# honour SNESRECOMP_DATA_DIR on macOS, so nothing is ever written inside the
# signed bundle (which may also be read-only under App Translocation).
cat > "$APPDIR/Contents/MacOS/$APP_NAME" <<LAUNCHER
#!/bin/sh
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
RES="\$(cd "\$DIR/../Resources" && pwd)"
DATA="\${SNESRECOMP_DATA_DIR:-\$HOME/Library/Application Support/$APP_NAME}"
mkdir -p "\$DATA/mods" || exit 1
export SNESRECOMP_DATA_DIR="\$DATA"
export SDL_JOYSTICK_HIDAPI_STEAM=1
# Release-owned assets: always point at this bundle's copy (survives moves and
# updates). Never replace a real directory the user created.
if [ -L "\$DATA/assets" ] || [ ! -e "\$DATA/assets" ]; then
  ln -sfn "\$RES/assets" "\$DATA/assets"
fi
# Seed/refresh the release-owned mod catalog; user-installed packages untouched.
cp -R "\$RES/mods/." "\$DATA/mods/" 2>/dev/null || true
chmod -R u+rwX "\$DATA/mods" 2>/dev/null || true
# A ROM dropped in the data folder or next to the .app seeds rom.cfg, so the
# launcher opens with it resolved (argv would skip the launcher entirely).
CACHED=""
[ -f "\$DATA/rom.cfg" ] && CACHED="\$(head -n1 "\$DATA/rom.cfg" 2>/dev/null | tr -d '\\r\\n')"
if [ -z "\$CACHED" ] || [ ! -f "\$CACHED" ]; then
  APPPARENT="\$(cd "\$DIR/../../.." && pwd)"
  for d in "\$DATA" "\$APPPARENT"; do
    for ext in $ROM_EXTS; do
      for f in "\$d"/*."\$ext"; do
        if [ -f "\$f" ]; then printf '%s\n' "\$f" > "\$DATA/rom.cfg"; break 3; fi
      done
    done
  done
fi
cd "\$DATA" || exit 1
# Opt-in diagnostics, driven by marker files beside the .app so nothing inside
# the signed bundle ever has to change:
#   diag.flag  -> console output to run.log and slow frames (>20 ms) to stall.csv
#   bench.flag -> one headless throughput + audio-paced benchmark into bench.log
#                 (the flag is consumed), then exit
APPPARENT="\$(cd "\$DIR/../../.." && pwd)"
if [ -f "\$APPPARENT/bench.flag" ]; then
  rm -f "\$APPPARENT/bench.flag"
  ROMP="\$(head -n1 rom.cfg 2>/dev/null | tr -d '\\r\\n')"
  for m in --benchmark --benchmark-audio-paced; do
    echo "--- \$m \$(date)" >> "\$APPPARENT/bench.log"
    "\$DIR/$CMAKE_TARGET" "\$ROMP" \$m 1800 --no-launcher >> "\$APPPARENT/bench.log" 2>&1
    echo "--- rc=\$?" >> "\$APPPARENT/bench.log"
  done
  exit 0
fi
if [ -f "\$APPPARENT/diag.flag" ]; then
  exec >>"\$APPPARENT/run.log" 2>&1
  echo "=== launch \$(date) DATA=\$DATA"
  export SNESRECOMP_STALL_LOG="\$APPPARENT/stall.csv" SNESRECOMP_STALL_MS=20
fi
exec "\$DIR/$CMAKE_TARGET" "\$@"
LAUNCHER
chmod +x "$APPDIR/Contents/MacOS/$APP_NAME"

cat > "$APPDIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$DISPLAY_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  $ICON_KEY
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PLIST

# Copy + relink a shared SDL (and any other non-system dylibs) into the bundle.
if otool -L "$APPDIR/Contents/MacOS/$CMAKE_TARGET" | grep -v -E '^\S|/usr/lib/|/System/' | grep -q '\.dylib'; then
  if command -v dylibbundler >/dev/null 2>&1; then
    dylibbundler -od -b -x "$APPDIR/Contents/MacOS/$CMAKE_TARGET" \
        -d "$APPDIR/Contents/Frameworks" -p @executable_path/../Frameworks
  else
    echo "      WARNING: dylibbundler not found — the .app needs the system SDL3 (brew install dylibbundler)."
  fi
fi
# Ad-hoc codesign so Gatekeeper lets it run locally (no Developer ID required).
codesign --force --deep --sign - "$APPDIR" 2>/dev/null || \
  echo "      (codesign skipped — run 'xattr -dr com.apple.quarantine' if Gatekeeper blocks it)"
echo "      BUILT: $APPDIR"

if [ "$DO_DMG" = "1" ]; then
  echo "[4/4] package .dmg"
  DMG="$OUT/$APP_NAME-$VERSION-macos-$ARCH.dmg"
  rm -f "$DMG"
  if command -v create-dmg >/dev/null 2>&1; then
    create-dmg --volname "$DISPLAY_NAME" --app-drop-link 420 180 "$DMG" "$APPDIR" || \
      hdiutil create -volname "$DISPLAY_NAME" -srcfolder "$APPDIR" -ov -format UDZO "$DMG"
  else
    hdiutil create -volname "$DISPLAY_NAME" -srcfolder "$APPDIR" -ov -format UDZO "$DMG"
  fi
  echo "      BUILT: $DMG"
else
  echo "[4/4] (--no-dmg) skipped"
fi
