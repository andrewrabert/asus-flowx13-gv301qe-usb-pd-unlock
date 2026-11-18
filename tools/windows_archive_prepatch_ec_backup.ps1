# Preserve the verified stock readouts before reusing the same proven backup EFI.
# No arming, reboot, firmware access, or deletion.
$ErrorActionPreference = 'Stop'
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$archive = Join-Path $audit 'backup-before-vid-20261118'
if (Test-Path $archive) { throw 'Archive already exists; inspect rather than overwrite' }
if (Test-Path (Join-Path $audit 'backup-probe-boot-entry.txt')) { throw 'Backup entry still pending' }
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
try {
    $esp = 'S:\EFI\FlowEcAudit'
    $espArchive = Join-Path $esp 'backup-before-vid-20261118'
    if (Test-Path $espArchive) { throw 'ESP archive already exists' }
    if (Test-Path (Join-Path $esp 'arm-backup-probe.txt')) { throw 'Backup token remains' }
    foreach ($dir in @($audit,$esp)) {
        foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin')) {
            if ((Get-FileHash (Join-Path $dir $name) -Algorithm SHA256).Hash -ne '5BC14FB3C61C9FA346210D18C67D45C93B7EF020E7D1C06369FE1D92A5FB7478') { throw "Stock backup mismatch: $dir $name" }
        }
    }
    New-Item -ItemType Directory -Path $archive | Out-Null
    New-Item -ItemType Directory -Path $espArchive | Out-Null
    foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin','backup-probe.txt')) {
        Move-Item (Join-Path $esp $name) (Join-Path $espArchive $name)
    }
    foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin','ec-backup-probe-collected.txt','backup-probe-boot-entry-completed.txt')) {
        if (Test-Path (Join-Path $audit $name)) { Move-Item (Join-Path $audit $name) (Join-Path $archive $name) }
    }
    Write-Output 'Verified original backups preserved in ESP and TEMP archives; backup reader can be reused.'
} finally { & mountvol.exe S: /D }
