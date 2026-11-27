#!/bin/bash
# One-shot macOS build: installs the missing tools, builds the .app and logs
# everything to macos/build.log. Run from Terminal:
#   bash ~/Downloads/SuperMarioWorldRecomp/macos/build-on-mac.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
LOG="$HERE/build.log"
exec > >(tee "$LOG") 2>&1
echo "=== $(date) — $(sw_vers -productName) $(sw_vers -productVersion) $(uname -m)"

# Homebrew (standard prefixes or a per-user install).
for b in /opt/homebrew/bin/brew /usr/local/bin/brew "$HOME/.homebrew/bin/brew" "$HOME/homebrew/bin/brew"; do
  [ -x "$b" ] && { eval "$("$b" shellenv)"; break; }
done

if ! xcode-select -p >/dev/null 2>&1; then
  echo "ERROR: Xcode Command Line Tools missing. Run: xcode-select --install"; exit 1
fi
if command -v brew >/dev/null 2>&1; then
  echo "brew: $(command -v brew)"
  for f in cmake sdl3 dylibbundler; do
    brew list --formula "$f" >/dev/null 2>&1 || brew install "$f" || echo "WARN: brew install $f failed"
  done
else
  echo "WARN: Homebrew not found."
  command -v cmake >/dev/null 2>&1 || {
    echo "ERROR: cmake missing. Install Homebrew (https://brew.sh) or CMake.app, then rerun."; exit 1; }
fi
echo "cmake: $(cmake --version | head -1)"; echo "python: $(python3 --version)"; echo "clang: $(clang --version | head -1)"

cd "$REPO"
# First build from a fresh clone: generate the recompiled C from the owner's ROM.
if ! ls src/gen/*.c >/dev/null 2>&1; then
  if [ ! -f smw.sfc ]; then
    echo "ERROR: src/gen/ is empty. Copy your Super Mario World (USA) ROM to"
    echo "       $REPO/smw.sfc and rerun this script."
    exit 1
  fi
  if ! command -v cargo >/dev/null 2>&1; then
    command -v brew >/dev/null 2>&1 && brew install rust
  fi
  command -v cargo >/dev/null 2>&1 || {
    echo "ERROR: Rust (cargo) is required once to generate src/gen. Install it (brew install rust) and rerun."; exit 1; }
  echo "=== generating src/gen from smw.sfc"
  bash tools/regen.sh --stock --no-tests || { echo "=== exit 1 (regen failed)"; exit 1; }
fi
bash tools/build-macos.sh --no-dmg "$@"
rc=$?
echo "=== exit $rc"
if [ $rc -eq 0 ]; then
  echo "App: $REPO/release-macos/SuperMarioWorld.app"
  open -R "$REPO/release-macos/SuperMarioWorld.app" 2>/dev/null || true
fi
exit $rc
