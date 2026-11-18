# Collects the preboot log and removes only our temporary boot entry.
$ErrorActionPreference = 'Stop'
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$state = Join-Path $audit 'smi-profile-boot-entry.txt'
if (!(Test-Path $state)) { throw 'No saved diagnostic boot entry' }
$id = (Get-Content $state -Raw).Trim()
if ($id -notmatch '^\{[0-9a-fA-F-]{36}\}$') { throw 'Invalid saved entry ID' }
$entry = (& bcdedit.exe /enum $id 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $entry -notmatch '\\EFI\\FlowEcAudit\\smi-profile.efi') {
    throw "Entry differs; will not delete: $entry"
}
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
try {
    $log = Get-Content 'S:\EFI\FlowEcAudit\smi-profile.txt' -Raw
    $log | Set-Content (Join-Path $audit 'ec-smi-profile-collected.txt')
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-smi-profile.txt') { throw 'Arm token remains: probe not consumed' }
    $hash = (Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash
    if ($hash -ne '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511') {
        throw 'Windows boot manager hash changed; inspect before cleanup'
    }
    & bcdedit.exe /delete $id
    if ($LASTEXITCODE -ne 0) { throw 'Temporary entry removal failed' }
    Move-Item $state (Join-Path $audit 'smi-profile-boot-entry-completed.txt')
    [ordered]@{ BIOS=(Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion;
        LastBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o');
        DiagnosticLog=[string]$log; WindowsBootManagerSHA256=$hash; RemovedEntry=$id } | ConvertTo-Json
    & bcdedit.exe /enum firmware
} finally {
    & mountvol.exe S: /D
}
