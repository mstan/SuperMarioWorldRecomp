# Super Mario World — macOS (Apple Silicon) build

Native arm64 macOS build of Super Mario World, attached to release **v0.8.2** as
`SuperMarioWorldRecomp-macos-arm64.zip`.

## What this is
- The original game statically recompiled to native arm64 (no emulator core shipped).
- Self-contained `.app`: SDL2 bundled via `@executable_path`, ad-hoc codesigned.
- Verified by manual play on Apple Silicon (looks/sounds correct on the golden path).


## Install
1. Download `SuperMarioWorldRecomp-macos-arm64.zip` from the **v0.8.2** release and unzip.
2. First launch: right-click `Super Mario World.app` -> Open (ad-hoc signed), or
   `xattr -dr com.apple.quarantine "Super Mario World.app"`.
3. ROM not included — supply your own dump: Super Mario World (USA) .sfc dump
4. Run: `"Super Mario World.app/Contents/MacOS/Super Mario World" /path/to/rom`

## Build it yourself
`scripts/release-mac.sh` reproduces this artifact (build -> .app -> zip);
`scripts/release-mac.sh --publish` re-attaches it to the latest release.
Requires: `brew install cmake ninja sdl2 dylibbundler` on Apple Silicon.
