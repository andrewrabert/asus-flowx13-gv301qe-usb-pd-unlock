# Stages and arms the indirect flash backup probe for one boot. Does not restart.
$ErrorActionPreference = 'Stop'
function Bcd([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return ($output -join "`n")
}
$audit = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\work'))
$state = Join-Path $audit 'backup-probe-boot-entry.txt'
if (Test-Path $state) { throw 'Existing diagnostic entry state; inspect before proceeding' }
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint C:).ProtectionStatus -ne 'Off') { throw 'BitLocker protection is enabled' }
if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE') { throw 'Wrong model' }
$before = Bcd @('/enum','firmware')
if ($before -match '(?im)^bootsequence\s') { throw 'A one-time boot is already selected' }
$before | Set-Content (Join-Path $audit 'backup-probe-firmware-before.txt')
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
$id = $null
try {
    if ((Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash -ne ((Get-Content (Join-Path $audit 'bootmgr.sha256') -Raw).Trim())) { throw 'Windows boot manager changed since Init' }
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus
    if (!($power | Where-Object PowerOnline)) { throw 'AC power not detected' }
    if ((Test-Path 'S:\EFI\FlowEcAudit\ec-backup-pass0.bin') -or (Test-Path 'S:\EFI\FlowEcAudit\ec-backup-pass1.bin')) { throw 'Existing backup; will not overwrite' }
    $source = Join-Path $audit 'backup-probe.efi'
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne '75AF4282AF1C86DD903D53961601DA3AEE13DB67C2EB6BB3A0701D8E5D4D9935') { throw 'Source hash mismatch' }
    Copy-Item $source 'S:\EFI\FlowEcAudit\backup-probe.efi'
    $hash = (Get-FileHash 'S:\EFI\FlowEcAudit\backup-probe.efi' -Algorithm SHA256).Hash
    if ($hash -ne '75AF4282AF1C86DD903D53961601DA3AEE13DB67C2EB6BB3A0701D8E5D4D9935') { throw 'Diagnostic hash mismatch' }
    if (Test-Path 'S:\EFI\FlowEcAudit\backup-probe.txt') { throw 'Unexpected existing log; archive before boot' }
    if (!(Test-Path 'S:\EFI\Microsoft\Boot\bootmgfw.efi')) { throw 'Windows boot manager missing' }
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-backup-probe.txt') { throw 'Unexpected existing arm token' }
    [IO.File]::WriteAllText('S:\EFI\FlowEcAudit\arm-backup-probe.txt', "GV301QE.415 INDIRECT BACKUP EC PROBE`n", [Text.Encoding]::ASCII)
    $copy = Bcd @('/copy','{bootmgr}','/d','Flow EC indirect flash backup probe (temporary)')
    $id = [regex]::Match($copy,'\{[0-9a-fA-F-]{36}\}').Value
    if (!$id) { throw "Cannot identify new entry: $copy" }
    $id | Set-Content $state
    Bcd @('/set',$id,'device','partition=S:') | Write-Output
    Bcd @('/set',$id,'path','\EFI\FlowEcAudit\backup-probe.efi') | Write-Output
    # Keep the normal persistent boot order. Only BootNext selects this entry.
    Bcd @('/set','{fwbootmgr}','displayorder',$id,'/remove') | Write-Output
    Bcd @('/set','{fwbootmgr}','bootsequence',$id) | Write-Output
    Bcd @('/enum','firmware') | Write-Output
} catch {
    Remove-Item 'S:\EFI\FlowEcAudit\arm-backup-probe.txt' -ErrorAction SilentlyContinue
    if ($id) {
        & bcdedit.exe /delete $id
        if ($LASTEXITCODE -eq 0) { Remove-Item $state -ErrorAction SilentlyContinue }
    }
    throw
} finally {
    & mountvol.exe S: /D
}
