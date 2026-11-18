# Stages and arms the read-only host SPI protection probe for one boot. Does not restart.
$ErrorActionPreference = 'Stop'
function Bcd([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return ($output -join "`n")
}
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$state = Join-Path $audit 'host-backup-boot-entry.txt'
if (Test-Path $state) { throw 'Existing diagnostic entry state; inspect before proceeding' }
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint C:).ProtectionStatus -ne 'Off') { throw 'BitLocker protection is enabled' }
if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE') { throw 'Wrong model' }
$before = Bcd @('/enum','firmware')
if ($before -match '(?im)^bootsequence\s') { throw 'A one-time boot is already selected' }
$before | Set-Content (Join-Path $audit 'host-backup-firmware-before.txt')
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
$id = $null
try {
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus
    if (!($power | Where-Object PowerOnline)) { throw 'AC power not detected' }
    foreach ($name in @('host-backup-pass0.bin','host-backup-pass1.bin')) {
        if (Test-Path (Join-Path 'S:\EFI\FlowEcAudit' $name)) { throw 'Existing backup file' }
    }
    if ((Get-PSDrive S).Free -lt 50MB) { throw 'Insufficient ESP space for two backups' }
    $source = Join-Path $audit 'host-backup.efi'
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne 'ABB02ADA0F7B353122D23F180067E020204AFD921AD7FE60449ACC2E5F0A2402') { throw 'Source hash mismatch' }
    Copy-Item $source 'S:\EFI\FlowEcAudit\host-backup.efi'
    $hash = (Get-FileHash 'S:\EFI\FlowEcAudit\host-backup.efi' -Algorithm SHA256).Hash
    if ($hash -ne 'ABB02ADA0F7B353122D23F180067E020204AFD921AD7FE60449ACC2E5F0A2402') { throw 'Diagnostic hash mismatch' }
    if (Test-Path 'S:\EFI\FlowEcAudit\host-backup.txt') { throw 'Unexpected existing log; archive before boot' }
    if (!(Test-Path 'S:\EFI\Microsoft\Boot\bootmgfw.efi')) { throw 'Windows boot manager missing' }
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-host-backup.txt') { throw 'Unexpected existing arm token' }
    [IO.File]::WriteAllText('S:\EFI\FlowEcAudit\arm-host-backup.txt', "GV301QE.415 READ ONLY HOST SPI HOST BACKUP V1`n", [Text.Encoding]::ASCII)
    $copy = Bcd @('/copy','{bootmgr}','/d','Flow host SPI protection audit (temporary)')
    $id = [regex]::Match($copy,'\{[0-9a-fA-F-]{36}\}').Value
    if (!$id) { throw "Cannot identify new entry: $copy" }
    $id | Set-Content $state
    Bcd @('/set',$id,'device','partition=S:') | Write-Output
    Bcd @('/set',$id,'path','\EFI\FlowEcAudit\host-backup.efi') | Write-Output
    # Keep the normal persistent boot order. Only BootNext selects this entry.
    Bcd @('/set','{fwbootmgr}','displayorder',$id,'/remove') | Write-Output
    Bcd @('/set','{fwbootmgr}','bootsequence',$id) | Write-Output
    Bcd @('/enum','firmware') | Write-Output
} catch {
    Remove-Item 'S:\EFI\FlowEcAudit\arm-host-backup.txt' -ErrorAction SilentlyContinue
    if ($id) {
        & bcdedit.exe /delete $id
        if ($LASTEXITCODE -eq 0) { Remove-Item $state -ErrorAction SilentlyContinue }
    }
    throw
} finally {
    & mountvol.exe S: /D
}
