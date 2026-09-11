param(
    [int]$Port = 4380,
    [switch]$Paused,
    [string]$RomPath,
    [string]$ExePath
)
$ErrorActionPreference = 'Stop'
$smwRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$RomPath) { $RomPath = Join-Path $smwRoot 'smw.sfc' }
if (!$ExePath) { $ExePath = Join-Path $smwRoot 'build-lua/SuperMarioWorldSNESRecomp.exe' }
$RomPath = (Resolve-Path -LiteralPath $RomPath).Path
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
if ($Port -lt 1 -or $Port -gt 65535) { throw 'Port must be 1..65535' }
$env:SNESRECOMP_LUA_PORT = "$Port"
$env:SNESRECOMP_LUA_PAUSED = if ($Paused) { '1' } else { '0' }
$env:SNESRECOMP_FORCE_TURBO = '0'
$env:SNESRECOMP_NO_LAUNCHER = '1'
# The local worktree build uses native MinGW; let its runtime DLLs resolve.
if (Test-Path -LiteralPath 'C:/msys64/mingw64/bin') {
    $env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
}
$runDir = Split-Path -Parent $ExePath
$game = Start-Process -FilePath $ExePath -ArgumentList ('"{0}"' -f $RomPath) `
    -WorkingDirectory $runDir -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $runDir "lua-$Port.stdout.log") `
    -RedirectStandardError (Join-Path $runDir "lua-$Port.stderr.log")
Write-Output "Started SMW PID $($game.Id); Lua TCP will listen on 127.0.0.1:$Port after boot."
