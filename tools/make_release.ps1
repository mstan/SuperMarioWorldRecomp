<#
make_release.ps1 — build the dual-zip release assets.

Every release ships TWO windows zips (and ONLY zips — never a bare exe;
smw.exe is useless without SDL2.dll):

  standard    SuperMarioWorldRecomp-windows-x64.zip
              Pristine gen (zero injected overrides), config Widescreen = 0.
              Byte-for-byte the authentic 256-wide recomp.

  widescreen  SuperMarioWorldRecomp-widescreen-windows-x64.zip
              apply_overrides.py gen (WS-FLAG / WS-DESPAWN / WS-SPAWN /
              WS-CHAIN / WS-SLOT block
              patches), config Widescreen = 1. The widescreen machinery is
              runtime-gated, but the standard zip is built from gen that
              never contained it at all — the defensible split.

The script drives the gen state itself via apply_overrides.py --restore /
apply, asserts the marker count in src/gen before each build (0 for
standard, -ExpectedInjections for widescreen), rebuilds, stages, zips.
Zips land in release\. Publish via gh AFTER the user signs off:

  gh release create vX.Y.Z release\SuperMarioWorldRecomp-windows-x64.zip `
      release\SuperMarioWorldRecomp-widescreen-windows-x64.zip

Usage: powershell -File tools\make_release.ps1 [-Variant standard|widescreen|both]
#>
param(
  [ValidateSet('standard', 'widescreen', 'both')]
  [string]$Variant = 'both',
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
  [int]$ExpectedInjections = 71
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin-x64-Release'
$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force $out | Out-Null

function Get-MarkerCount {
  $hits = Select-String -Path (Join-Path $root 'src\gen\*.c') `
      -Pattern '/\*WS-(OVERRIDE|FLAG|DESPAWN|SPAWN|CHAIN|SLOT)\*/' -AllMatches
  $sum = ($hits | ForEach-Object { $_.Matches.Count } | Measure-Object -Sum).Sum
  if ($null -eq $sum) { return 0 } else { return [int]$sum }
}

function Set-GenState([string]$kind) {
  python (Join-Path $root 'tools\apply_overrides.py') --restore --gen-dir (Join-Path $root 'src\gen')
  if ($LASTEXITCODE -ne 0) { throw 'apply_overrides --restore failed' }
  $expected = 0
  if ($kind -eq 'widescreen') {
    python (Join-Path $root 'tools\apply_overrides.py') -v --check `
        --gen-dir (Join-Path $root 'src\gen') `
        --manifest (Join-Path $root 'overrides\widescreen\overrides.manifest')
    if ($LASTEXITCODE -ne 0) { throw 'apply_overrides failed' }
    $expected = $ExpectedInjections
  }
  $count = Get-MarkerCount
  if ($count -ne $expected) {
    throw "gen marker count $count != expected $expected for $kind build"
  }
  # Force recompilation of every gen TU. MSBuild is timestamp-based and the
  # gen state changes by CONTENT: a no-op restore (already-clean gen, e.g.
  # freshly copied with Copy-Item's preserved old timestamps) leaves .c
  # files older than .obj files compiled from the OTHER state, and the
  # build silently links stale objects (observed: the standard zip shipped
  # the widescreen exe).
  Get-ChildItem (Join-Path $root 'src\gen\*.c') | ForEach-Object {
    $_.LastWriteTime = Get-Date
  }
  Write-Host "gen state: $kind ($count injection markers)"
}

function Invoke-Build {
  & $MSBuild (Join-Path $root 'smw.sln') /p:Configuration=Release /p:Platform=x64 /m /v:quiet /nologo
  if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }
}

$readmeCommon = @'
Super Mario World - Static Recompilation
========================================
1. Run smw.exe.
2. When prompted, select your legally-obtained Super Mario World
   (USA) ROM (.sfc / .smc). The path is remembered in rom.cfg
   next to the exe.
3. Controller buttons are mapped in keybinds.ini (regenerated next
   to the exe if deleted). System shortcuts (save/load state,
   fullscreen, turbo...) live in config.ini's [KeyMap] section.

Default controls:
  D-Pad     = Arrow keys
  A         = X        Y = A        L = C
  B         = Z        X = S        R = V
  Start     = Enter    Select = Right Shift

Save states: Shift+F1..F10 saves, F1..F10 loads. In-game saves
land in saves\smw.srm. Alt+Enter = fullscreen, Tab = turbo,
Ctrl+R = reset.

ROM not included - provide your own dump.
See https://github.com/mstan/SuperMarioWorldRecomp for source.
'@

$readmeVariant = @{
  'standard' = @'

This is the STANDARD build: the authentic 256-pixel-wide SNES
presentation, with none of the optional widescreen extension
compiled in. A separate widescreen zip is published alongside
each release if you want 16:9.
'@
  'widescreen' = @'

This is the WIDESCREEN build: the game renders the extra columns
needed to fill your window's aspect ratio (up to ~2.0:1, a hard
limit of the SNES's 9-bit sprite coordinates), and SMW's own
spawn/cull logic is widened so enemies populate the wider view.
Resize the window or go fullscreen and the view adapts live.

config.ini options:
  Widescreen    = 1  the wide view (set 0 for the authentic look)
  WidescreenHud = 1  anchor the status bar to the screen edges
                     (set 0 to keep it centered at 4:3)

The attract demo and non-level screens stay authentic 4:3 by
design - widescreen applies in player-controlled levels.
'@
}

function New-ReleaseZip([string]$kind) {
  $stage = Join-Path $out "stage-$kind"
  if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
  New-Item -ItemType Directory -Force $stage | Out-Null

  Copy-Item (Join-Path $bin 'smw.exe') $stage
  Copy-Item (Join-Path $bin 'SDL2.dll') $stage
  $kb = Join-Path $bin 'keybinds.ini'
  if (Test-Path $kb) { Copy-Item $kb $stage }

  # Repo config.ini with Widescreen forced per variant. (WidescreenHud's
  # default of 1 only matters when Widescreen is on; ship it untouched.)
  $val = 0
  if ($kind -eq 'widescreen') { $val = 1 }
  (Get-Content (Join-Path $root 'config.ini')) `
      -replace '^Widescreen\s*=.*$', "Widescreen = $val" |
      Out-File (Join-Path $stage 'config.ini') -Encoding ascii

  ($readmeCommon + $readmeVariant[$kind]) |
      Out-File (Join-Path $stage 'README.txt') -Encoding ascii

  $zipName = if ($kind -eq 'widescreen') {
    'SuperMarioWorldRecomp-widescreen-windows-x64.zip'
  } else {
    'SuperMarioWorldRecomp-windows-x64.zip'
  }
  $zip = Join-Path $out $zipName
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Compress-Archive -Path "$stage\*" -DestinationPath $zip
  Write-Host "--- $kind zip ---"
  Get-ChildItem $stage | Select-Object Name, Length | Out-Host
  Get-Item $zip | Select-Object Name, Length | Out-Host
}

$kinds = if ($Variant -eq 'both') { @('standard', 'widescreen') } else { @($Variant) }
foreach ($kind in $kinds) {
  Set-GenState $kind
  Invoke-Build
  New-ReleaseZip $kind
}
Write-Host "done. gen left in '$($kinds[-1])' state; zips in $out"
