<#
Package a completed Super Mario World Windows release build. Pass
`-Variant coop` to stage the simultaneous co-op executable and IPS.

The build itself is intentionally separate so developers can choose their
toolchain and keep compilation priority under local control. The resulting zip
contains the executable, MinGW runtime dependencies, recomp-ui launcher
assets, configuration, and README. ROMs and ROM-derived generated C are never
staged.

Ships ONE windows zip (and ONLY a zip - never a bare exe; the exe is useless
without its SDL runtime and the recomp-ui assets/ next to it):

  SuperMarioWorldRecomp-windows-x64-v<Version>.zip
  SuperMarioWorldCoopSNESRecomp-windows-x64-v<Version>.zip

This script does NOT build. Build first, e.g.:
  export PATH=/c/msys64/mingw64/bin:$PATH
  cmake --build build-recompui -j

The archive is written with portable ('/') ZIP entry names and then re-read to
reject any Windows-only entry name. This is what lets Linux/Steam Deck
extractors rebuild the nested assets/ (and mods/packages/) hierarchy, so a
Windows build running under Proton finds its bundled launcher assets.

Zips land in release-stage\. Publish via gh only after review/sign-off.

Example:
  powershell -File tools\make_release.ps1 -Version 0.9.0 `
    -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin

  powershell -File tools\make_release.ps1 -Version 0.9.0 -Variant coop `
    -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [ValidateSet('stock', 'coop')][string]$Variant = 'stock',
  [string]$BuildDir = 'build-recompui',
  [string]$RuntimeBinDir = 'C:\msys64\mingw64\bin',
  [ValidateSet('SDL3', 'SDL2')][string]$SdlBackend = 'SDL3'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$exeName = if ($Variant -eq 'coop') { 'SuperMarioWorldCoopSNESRecomp.exe' } else { 'SuperMarioWorldSNESRecomp.exe' }
$exe = Join-Path $build $exeName
$assets = Join-Path $build 'assets'
$mods = Join-Path $build 'mods'

if (-not (Test-Path -LiteralPath $exe)) {
  throw "Release executable missing: $exe"
}
if (-not (Test-Path -LiteralPath $assets)) {
  throw "recomp-ui launcher assets/ missing: $assets"
}

# The exe must actually carry the version it is being packaged as. host_report
# stamps crash breadcrumbs with SNESRECOMP_BUILD_VERSION, so a build configured
# without it (or with it mangled) would ship reports that cannot be tied to this
# release. PowerShell silently rewrites an unquoted `-DSNESRECOMP_BUILD_VERSION=
# 0.10.0` to `0`, which is exactly how that happens in practice — always pass
# `"-DSNESRECOMP_BUILD_VERSION:STRING=<version>"` quoted.
$exeText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
if (-not $exeText.Contains($Version)) {
  throw ("Release executable is not stamped with version '$Version'. " +
    "Reconfigure with `"-DSNESRECOMP_BUILD_VERSION:STRING=$Version`" " +
    'and rebuild before packaging.')
}

$out = Join-Path $root 'release-stage'
$stageBase = if ($Variant -eq 'coop') { 'SuperMarioWorldCoopSNESRecomp' } else { 'SuperMarioWorldRecomp' }
$stageName = "$stageBase-windows-x64-v$Version"
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
if ($Variant -eq 'coop') {
  Copy-Item -LiteralPath (Join-Path $root 'recomp\coop\smw_coop.ips') -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
Copy-Item -LiteralPath $assets -Destination $stage -Recurse
# Release-owned mod catalog, when the build stages one. Ships as a nested
# directory tree, which is exactly what made portable ZIP entry names matter
# (see the archive writer below).
if (Test-Path -LiteralPath $mods) {
  Copy-Item -LiteralPath $mods -Destination $stage -Recurse
}

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
  'libgcc_s_seh-1.dll',
  'libstdc++-6.dll',
  'libwinpthread-1.dll'
)
$sdlDll = "$SdlBackend.dll"
$sdlSource = Join-Path $build $sdlDll
if (-not (Test-Path -LiteralPath $sdlSource)) {
  $sdlSource = Join-Path $RuntimeBinDir $sdlDll
}
if (-not (Test-Path -LiteralPath $sdlSource)) {
  throw "Required $SdlBackend runtime DLL missing from build or runtime bin: $sdlDll"
}
Copy-Item -LiteralPath $sdlSource -Destination $stage
foreach ($name in $runtimeDlls) {
  $source = Join-Path $RuntimeBinDir $name
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Required MinGW runtime DLL missing: $source"
  }
  Copy-Item -LiteralPath $source -Destination $stage
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ZIP entry names always use '/', regardless of the host OS. Compress-Archive
# preserves Windows backslashes, which POSIX extractors may treat as literal
# filename characters instead of directory separators. That is what stopped
# Linux/Steam Deck extractors from creating the nested assets/ and
# mods/packages/ hierarchies, so a Windows build running under Proton could not
# discover its bundled launcher assets or mod catalog.
$stagePrefix = $stageFull.TrimEnd('\') + '\'
$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse |
  Sort-Object FullName)
$archive = [IO.Compression.ZipFile]::Open(
  $zipFull, [IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($file in $files) {
    $fileFull = [IO.Path]::GetFullPath($file.FullName)
    if (-not $fileFull.StartsWith(
        $stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Refusing to archive a file outside the release stage: $fileFull"
    }
    $entryName = $fileFull.Substring($stagePrefix.Length).Replace('\', '/')
    if ($entryName.StartsWith('/') -or
        $entryName -match '(^|/)\.\.(/|$)') {
      throw "Unsafe ZIP entry name: $entryName"
    }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
      $archive, $fileFull, $entryName,
      [IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
} finally {
  $archive.Dispose()
}

# Read the archive back and reject non-portable entry names outright, so a
# regression in the writer cannot ship a zip that only extracts on Windows.
$archive = [IO.Compression.ZipFile]::OpenRead($zipFull)
try {
  $badEntries = @($archive.Entries | Where-Object {
    $_.FullName.Contains('\') -or
    $_.FullName.StartsWith('/') -or
    $_.FullName -match '(^|/)\.\.(/|$)'
  })
  if ($badEntries.Count -ne 0) {
    throw "ZIP contains non-portable entry names: $(
      ($badEntries | ForEach-Object FullName) -join ', ')"
  }
  if ($archive.Entries.Count -ne $files.Count) {
    throw "ZIP entry count mismatch: expected $($files.Count), got $(
      $archive.Entries.Count)"
  }
} finally {
  $archive.Dispose()
}

Write-Host "--- $stageName ---"
Get-ChildItem -LiteralPath $stage | Select-Object Name, Length | Out-Host
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Out-Host
