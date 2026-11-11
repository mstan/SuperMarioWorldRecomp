param([switch]$NoLauncher, [ValidateRange(100,30000)][int]$DurationMs=10000)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$game=Join-Path $root 'SuperMarioWorldSNESRecomp.exe'
$helper=Join-Path $root 'smw-controller-report.exe'
$folder=Join-Path $root 'diagnostics'
try {
    New-Item -ItemType Directory -Path $folder -Force -ErrorAction Stop | Out-Null
    $probe=Join-Path $folder ('.controller-write-'+[guid]::NewGuid().ToString('N')+'.tmp')
    $stream=[IO.File]::Open($probe,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    $stream.Dispose()
    Remove-Item -LiteralPath $probe -ErrorAction Stop
}
catch {
    $folder=Join-Path $env:LOCALAPPDATA 'SuperMarioWorldRecomp\diagnostics'
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
}
$stamp=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss-fff')+'-'+$PID
$raw=Join-Path $folder ('controller-'+$stamp+'-devices.jsonl')
$launcher=Join-Path $folder ('controller-'+$stamp+'-launcher.jsonl')
$identity=Join-Path $folder ('controller-'+$stamp+'-build.txt')
$windows=Join-Path $folder ('controller-'+$stamp+'-windows.json')
$zip=Join-Path $folder ('controller-'+$stamp+'.zip')
foreach ($file in $helper,$game) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing $file. Extract the complete download first." }
}
$lines=@('Super Mario World controller report')
foreach ($file in $helper,$game) {
    $lines+=([IO.Path]::GetFileName($file)+' SHA256='+ (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)
}
[IO.File]::WriteAllLines($identity,$lines,(New-Object Text.UTF8Encoding($false)))
$deviceRows=@()
$queryStatus='success'
try {
    $devices=Get-CimInstance Win32_PnPEntity -Filter "PNPClass='HIDClass' OR PNPClass='USB' OR PNPClass='XnaComposite'"
    foreach ($device in $devices) {
        if ($device.DeviceID -match 'VID_([0-9A-F]{4}).*PID_([0-9A-F]{4})') {
            $deviceRows+=[pscustomobject]@{
                name=$device.Name; vendor=$Matches[1]; product=$Matches[2]
                class=$device.PNPClass; service=$device.Service; status=$device.Status
                errorCode=$device.ConfigManagerErrorCode
            }
        }
    }
} catch { $queryStatus='failed: '+$_.Exception.Message }
$windowsReport=[pscustomobject]@{
    query=$queryStatus; count=$deviceRows.Count; truncated=($deviceRows.Count -gt 256)
    devices=@($deviceRows | Select-Object -First 256)
}
[IO.File]::WriteAllText($windows,($windowsReport | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
Write-Output 'Connect the controller. During this capture, press/release buttons and move the sticks and D-pad.'
$start=New-Object Diagnostics.ProcessStartInfo
$start.FileName=$helper
$start.Arguments='"'+$raw+'" '+$DurationMs
$start.WorkingDirectory=$root
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$process=[Diagnostics.Process]::Start($start)
try { $process.WaitForExit(); if ($process.ExitCode) { throw 'Controller capture failed.' } }
finally { $process.Dispose() }
if (-not $NoLauncher) {
    $oldReport=$env:RECOMP_INPUT_REPORT
    $oldDuration=$env:RECOMP_INPUT_REPORT_MS
    try {
        $env:RECOMP_INPUT_REPORT=$launcher
        $env:RECOMP_INPUT_REPORT_MS='30000'
        Write-Output 'The launcher will open. Check the controller list and bindings, then close it. No ROM or save-file selection is needed.'
        $start=New-Object Diagnostics.ProcessStartInfo
        $start.FileName=$game
        $start.WorkingDirectory=$root
        $start.UseShellExecute=$false
        $process=[Diagnostics.Process]::Start($start)
        try { $process.WaitForExit() } finally { $process.Dispose() }
    } finally {
        $env:RECOMP_INPUT_REPORT=$oldReport
        $env:RECOMP_INPUT_REPORT_MS=$oldDuration
    }
}
$files=@($raw,$launcher,$identity,$windows) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
Compress-Archive -LiteralPath $files -DestinationPath $zip
Write-Output "Report ready: $zip"
