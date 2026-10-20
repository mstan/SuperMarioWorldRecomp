[CmdletBinding()]
param(
    [string]$DirectRomPath,
    [string]$RuntimeBin = 'C:\msys64\mingw64\bin',
    [string[]]$GameArguments = @(),
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$rendererRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$rendererExe = Join-Path $rendererRoot 'build-adaptive\SuperMarioWorldSNESRecomp.exe'
$rendererData = Join-Path $rendererRoot 'build-adaptive\playtest'
if (-not (Test-Path -LiteralPath $rendererExe -PathType Leaf)) { throw "Build first: $rendererExe" }
if (-not (Test-Path -LiteralPath $RuntimeBin -PathType Container)) { throw "Missing runtime DLL directory: $RuntimeBin" }
if ($CheckOnly) {
    Write-Output "Executable: $rendererExe"
    Write-Output "Settings and saves: $rendererData"
    return
}
[void](New-Item -ItemType Directory -Path $rendererData -Force)
$rendererMods = Join-Path $rendererData 'mods'
[void](New-Item -ItemType Directory -Path $rendererMods -Force)
Copy-Item -LiteralPath (Join-Path $rendererRoot 'mods\preloaded') -Destination $rendererMods -Recurse -Force
$rendererConfig = Join-Path $rendererData 'config.ini'
if (-not (Test-Path -LiteralPath $rendererConfig)) {
    @"
[General]
Autosave=0
SkipLauncher=0
[Graphics]
WindowSize=1280x720
WindowScale=2
NewRenderer=1
NoSpriteLimits=1
[Sound]
EnableAudio=1
"@ | Set-Content -LiteralPath $rendererConfig -Encoding ascii
}
$rendererState = Join-Path $rendererMods 'preloaded\state.toml'
if (-not (Test-Path -LiteralPath $rendererState)) {
    @'
format_version = 1
[[package]]
id = "super-mario-world.enhancement.widescreen"
version = "1.0.0"
[[feature]]
package_id = "super-mario-world.enhancement.widescreen"
id = "widescreen"
enabled = true
[feature.values]
mode = "adaptive"
spawn = "adaptive"
'@ | Set-Content -LiteralPath $rendererState -Encoding ascii
}
$rendererCachedRom = Join-Path $rendererData 'rom.cfg'
$rendererLocalRom = Join-Path $rendererRoot 'smw.sfc'
if (-not (Test-Path -LiteralPath $rendererCachedRom) -and (Test-Path -LiteralPath $rendererLocalRom)) {
    Set-Content -LiteralPath $rendererCachedRom -Value $rendererLocalRom -Encoding ascii
}
$rendererArguments = @('--config', $rendererConfig)
if ($DirectRomPath) { $rendererArguments += (Resolve-Path -LiteralPath $DirectRomPath).Path }
$rendererArguments += $GameArguments
$savedRendererPath = $env:PATH
Push-Location -LiteralPath $rendererData
try {
    $env:PATH = "$RuntimeBin;$savedRendererPath"
    & $rendererExe @rendererArguments
    if ($LASTEXITCODE -ne 0) { throw "Renderer exited with code $LASTEXITCODE; logs are in $rendererData" }
} finally {
    $env:PATH = $savedRendererPath
    Pop-Location
}
