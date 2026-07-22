<#
Package a completed Super Mario World Windows release build.

The build itself is intentionally separate so developers can choose their
toolchain and keep compilation priority under local control. The resulting zip
contains the executable, MinGW runtime dependencies, recomp-ui launcher
assets, configuration, and README. ROMs and ROM-derived generated C are never
staged.

Ships ONE windows zip (and ONLY a zip - never a bare exe; the exe is useless
without SDL2.dll and the recomp-ui assets/ next to it):

  SuperMarioWorldCoopSNESRecomp-windows-x64-v<Version>.zip

The simultaneous co-op ROM changes offsets used by the stock-game widescreen
hooks, so this build exposes only the authentic 256-wide mode. Widescreen
remains available in the standard one-player build.

This script does NOT build. Build first, e.g.:
  export PATH=/c/msys64/mingw64/bin:$PATH
  cmake --build build-recompui -j

Zips land in release-stage\. Publish via gh AFTER the user signs off:

  gh release create v<Version> release-stage\SuperMarioWorldCoopSNESRecomp-windows-x64-v<Version>.zip

Example:
  powershell -File tools\make_release.ps1 -Version 0.9.0 `
    -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = 'build-recompui',
  [string]$RuntimeBinDir = 'C:\msys64\mingw64\bin'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'SuperMarioWorldCoopSNESRecomp.exe'
$assets = Join-Path $build 'assets'

if (-not (Test-Path -LiteralPath $exe)) {
  throw "Release executable missing: $exe"
}
if (-not (Test-Path -LiteralPath $assets)) {
  throw "recomp-ui launcher assets/ missing: $assets"
}

$out = Join-Path $root 'release-stage'
$stageName = "SuperMarioWorldCoopSNESRecomp-windows-x64-v$Version"
$stage = Join-Path $out $stageName
$zip = Join-Path $out "$stageName.zip"

$outFull = [IO.Path]::GetFullPath($out).TrimEnd('\') + '\'
$stageFull = [IO.Path]::GetFullPath($stage)
$zipFull = [IO.Path]::GetFullPath($zip)
if (-not $stageFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase) -or
    -not $zipFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Refusing to clean release paths outside release-stage.'
}

if (Test-Path -LiteralPath $stage) {
  Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
  Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'recomp\coop\smw_coop.ips') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
Copy-Item -LiteralPath $assets -Destination $stage -Recurse

# keybinds.ini is auto-generated next to the exe on first run (regenerated if
# deleted); ship whatever is currently sitting next to the built exe, if any.
$kb = Join-Path $build 'keybinds.ini'
if (Test-Path -LiteralPath $kb) {
  Copy-Item -LiteralPath $kb -Destination $stage
}

# config.ini ships Widescreen = 0 regardless of the repo's working-tree value
# (a dev may have flipped it locally while testing); the launcher toggles +
# persists the player's choice at runtime.
(Get-Content (Join-Path $root 'config.ini')) -replace '^Widescreen\s*=.*$', 'Widescreen = 0' |
  Out-File (Join-Path $stage 'config.ini') -Encoding ascii

$runtimeDlls = @(
  'SDL2.dll',
  'libgcc_s_seh-1.dll',
  'libstdc++-6.dll',
  'libwinpthread-1.dll'
)
foreach ($name in $runtimeDlls) {
  $source = Join-Path $RuntimeBinDir $name
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Required MinGW runtime DLL missing: $source"
  }
  Copy-Item -LiteralPath $source -Destination $stage
}

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host "--- $stageName ---"
Get-ChildItem -LiteralPath $stage | Select-Object Name, Length | Out-Host
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Out-Host
