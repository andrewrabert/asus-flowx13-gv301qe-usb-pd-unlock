# Collects the preboot log and removes only our temporary boot entry.
$ErrorActionPreference = 'Stop'
$audit = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\work'))
$state = Join-Path $audit 'backup-probe-boot-entry.txt'
if (!(Test-Path $state)) { throw 'No saved diagnostic boot entry' }
$id = (Get-Content $state -Raw).Trim()
if ($id -notmatch '^\{[0-9a-fA-F-]{36}\}$') { throw 'Invalid saved entry ID' }
$entry = (& bcdedit.exe /enum $id 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $entry -notmatch '\\EFI\\FlowEcAudit\\backup-probe.efi') {
    throw "Entry differs; will not delete: $entry"
}
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
try {
    $log = Get-Content 'S:\EFI\FlowEcAudit\backup-probe.txt' -Raw
    $log | Set-Content (Join-Path $audit 'ec-backup-probe-collected.txt')
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-backup-probe.txt') { throw 'Arm token remains: probe not consumed' }
    foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin')) {
        if (Test-Path (Join-Path 'S:\EFI\FlowEcAudit' $name)) { Copy-Item (Join-Path 'S:\EFI\FlowEcAudit' $name) (Join-Path $audit $name) }
    }
    $hash = (Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash
    if ($hash -ne ((Get-Content (Join-Path $audit 'bootmgr.sha256') -Raw).Trim())) {
        throw 'Windows boot manager hash changed; inspect before cleanup'
    }
    & bcdedit.exe /delete $id
    if ($LASTEXITCODE -ne 0) { throw 'Temporary entry removal failed' }
    Move-Item $state (Join-Path $audit 'backup-probe-boot-entry-completed.txt')
    [ordered]@{ BIOS=(Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion;
        LastBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o');
        DiagnosticLog=[string]$log; WindowsBootManagerSHA256=$hash; RemovedEntry=$id } | ConvertTo-Json
    & bcdedit.exe /enum firmware
} finally {
    & mountvol.exe S: /D
}
