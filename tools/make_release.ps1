<#
make_release.ps1 - build the Super Mario World windows release zip.

Ships ONE windows zip (and ONLY a zip - never a bare exe; smw.exe is useless
without SDL2.dll and the launcher/ assets):

  SuperMarioWorldRecomp-windows-x64-v<Version>.zip

CONSOLIDATED (was dual standard/widescreen zips): the GUI launcher now has a
Widescreen 16:9 toggle and persists it to config.ini, so a separate widescreen
zip is redundant - one build serves both. The widescreen game-logic overrides
(apply_overrides.py, 91 runtime-gated patches) are injected into src/gen, but
config.ini ships Widescreen = 0, so by default the game is authentic 256-wide
and the overrides are inert (gated on g_ws_active). The player flips widescreen
in the launcher.

The gen this builds from is the MSU-1 build (recompiled from Conn's audio-only
SMW MSU-1 patch); the MSU-1 driver runs on the stock ROM (authentic SPC audio
with no pack). This script does NOT regen - run tools/regen.sh first if src/gen
is stale; it then injects the widescreen overrides, builds Production, and zips.

Zips land in release\. Publish via gh AFTER the user signs off:

  gh release create v<Version> release\SuperMarioWorldRecomp-windows-x64-v<Version>.zip

Usage: powershell -File tools\make_release.ps1 -Version 0.9.0
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
  [int]$ExpectedInjections = 95
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin-x64-Production'
$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force $out | Out-Null

function Get-MarkerCount {
  $hits = Select-String -Path (Join-Path $root 'src\gen\*.c') `
      -Pattern '/\*WS-(OVERRIDE|FLAG|DESPAWN|SPAWN|CHAIN|SLOT|RELOC|WING)\*/' -AllMatches
  $sum = ($hits | ForEach-Object { $_.Matches.Count } | Measure-Object -Sum).Sum
  if ($null -eq $sum) { return 0 } else { return [int]$sum }
}

# Inject the widescreen overrides (runtime-gated) into the current src/gen.
python (Join-Path $root 'tools\apply_overrides.py') --restore --gen-dir (Join-Path $root 'src\gen')
if ($LASTEXITCODE -ne 0) { throw 'apply_overrides --restore failed' }
python (Join-Path $root 'tools\apply_overrides.py') -v --check `
    --gen-dir (Join-Path $root 'src\gen') `
    --manifest (Join-Path $root 'overrides\widescreen\overrides.manifest')
if ($LASTEXITCODE -ne 0) { throw 'apply_overrides failed' }
$count = Get-MarkerCount
if ($count -ne $ExpectedInjections) { throw "gen marker count $count != expected $ExpectedInjections" }
# Force recompile of every gen TU (MSBuild is timestamp-based; gen changed by content).
Get-ChildItem (Join-Path $root 'src\gen\*.c') | ForEach-Object { $_.LastWriteTime = Get-Date }
Write-Host "gen state: widescreen overrides injected ($count markers)"

# Production|x64: the shipped config (SNESRECOMP_TRACE OFF, console-free). Builds
# the launcher + stages its assets into build\bin-x64-Production\launcher\.
# SnesRecompBuildVersion stamps SNESRECOMP_BUILD_VERSION into the exe so
# user crash reports (last_run_report.json / crash_report_*.json) name the
# release they came from.
& $MSBuild (Join-Path $root 'smw.sln') /p:Configuration=Production /p:Platform=x64 "/p:SnesRecompBuildVersion=$Version" /m /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }

$stageName = "SuperMarioWorldRecomp-windows-x64-v$Version"
$stage = Join-Path $out $stageName
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin 'smw.exe') $stage
Copy-Item (Join-Path $bin 'SDL2.dll') $stage
$kb = Join-Path $bin 'keybinds.ini'
if (Test-Path $kb) { Copy-Item $kb $stage }
# config.ini ships Widescreen = 0; the launcher toggles + persists it.
(Get-Content (Join-Path $root 'config.ini')) -replace '^Widescreen\s*=.*$', 'Widescreen = 0' |
  Out-File (Join-Path $stage 'config.ini') -Encoding ascii

# Launcher assets (RmlUi) - the GUI menu needs these next to the exe.
$launcherSrc = Join-Path $bin 'launcher'
if (-not (Test-Path $launcherSrc)) { throw "launcher/ assets missing at $launcherSrc - did the Production build run CopyLauncherAssets?" }
Copy-Item $launcherSrc $stage -Recurse

@'
Super Mario World - Static Recompilation
========================================
1. Run smw.exe. A launcher opens: pick your legally-obtained Super Mario
   World (USA) ROM (.sfc / .smc), tune settings, and press PLAY. The path
   is remembered in rom.cfg next to the exe.
2. Controller buttons are mapped in keybinds.ini (regenerated next to the
   exe if deleted). Settings persist to config.ini.

Default controls:
  D-Pad = Arrow keys   A = X   B = Z   X = S   Y = A   L = C   R = V
  Start = Enter        Select = Right Shift

Save states: Shift+F1..F10 saves, F1..F10 loads. Alt+Enter = fullscreen,
Tab = turbo, Ctrl+R = reset.

Widescreen 16:9 and MSU-1 audio are toggles in the launcher (Settings).
For MSU-1, drop a "SMW MSU-1" (Conn / zeldix t1436) PCM pack into the msu\
folder next to the exe - packs for other SMW MSU-1 patches will not match.
No pack = authentic SPC music. MSU-1 driver credit: Conn (see the repo's
recomp/msu1/ATTRIBUTION.md).

ROM not included - provide your own dump.
See https://github.com/mstan/SuperMarioWorldRecomp for source.
'@ | Out-File (Join-Path $stage 'README.txt') -Encoding ascii

$zip = Join-Path $out "$stageName.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

# Archive the PDB NEXT TO the zip (never inside it): it's what turns a
# user's crash_minidump_*.dmp / module+offset stack into file:line. Keep
# it with the release artifacts forever.
$pdb = Join-Path $bin 'smw.pdb'
if (Test-Path $pdb) {
  Copy-Item $pdb (Join-Path $out "smw-v$Version.pdb")
  Write-Host "PDB archived: $out\smw-v$Version.pdb (do NOT ship; keep for symbolizing user crash dumps)"
} else {
  Write-Warning "smw.pdb missing from $bin - crash minidumps from this release won't symbolize."
}
Write-Host "--- $stageName ---"
Get-ChildItem $stage | Select-Object Name, Length | Out-Host
Get-Item $zip | Select-Object Name, Length | Out-Host
