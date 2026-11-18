# Stages only. Does not create boot entries, select a next boot, or reboot.
$ErrorActionPreference = 'Stop'
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
$source = Join-Path $env:TEMP 'flowx13-ec-audit\status.efi'
$hash = 'B97AAFD6C6880597E451485B191103B0222908E19AD1CE4CBE332E94D6FA8B77'
if ((Get-FileHash $source -Algorithm SHA256).Hash -ne $hash) { throw 'Wrong diagnostic binary' }
if (Test-Path 'S:\') { throw 'S: is occupied; choose a free mount point before staging' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
try {
    $boot = 'S:\EFI\Microsoft\Boot\bootmgfw.efi'
    if (!(Test-Path $boot)) { throw 'Windows boot manager absent from this ESP' }
    $before = (Get-FileHash $boot -Algorithm SHA256).Hash
    $dest = 'S:\EFI\FlowEcAudit'
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item $source (Join-Path $dest 'status.efi') -Force
    $actual = (Get-FileHash (Join-Path $dest 'status.efi') -Algorithm SHA256).Hash
    if ($actual -ne $hash) { throw 'Copy verification failed' }
    if ((Get-FileHash $boot -Algorithm SHA256).Hash -ne $before) { throw 'Boot manager changed' }
    [ordered]@{ Diagnostic='\EFI\FlowEcAudit\status.efi'; SHA256=$actual;
        WindowsBootManagerSHA256=$before; BootEntryChanged=$false; Rebooted=$false } | ConvertTo-Json
} finally {
    & mountvol.exe S: /D
    if ($LASTEXITCODE -ne 0) { Write-Warning 'Could not unmount S:' }
}
