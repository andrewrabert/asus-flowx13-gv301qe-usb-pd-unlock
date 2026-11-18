# EXPERIMENTAL: Apply five reviewed VID-patch bytes across sectors 03D000 and 042000.
# Stages and arms the stock SMI VID-only two-sector update probe for one boot. Does not restart.
$ErrorActionPreference = 'Stop'
function Bcd([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return ($output -join "`n")
}
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$state = Join-Path $audit 'smi-vid-patch-boot-entry.txt'
if (Test-Path $state) { throw 'Existing diagnostic entry state; inspect before proceeding' }
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint C:).ProtectionStatus -ne 'Off') { throw 'BitLocker protection is enabled' }
if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE') { throw 'Wrong model' }
$before = Bcd @('/enum','firmware')
if ($before -match '(?im)^bootsequence\s') { throw 'A one-time boot is already selected' }
$before | Set-Content (Join-Path $audit 'smi-vid-patch-firmware-before.txt')
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
$id = $null
try {
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus
    if (!($power | Where-Object PowerOnline)) { throw 'AC power not detected' }
    if ((Get-CimInstance Win32_Battery).EstimatedChargeRemaining -lt 80) { throw 'Battery below 80 percent' }
    if ((Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash -ne '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511') { throw 'Windows boot manager hash mismatch' }
    $source = Join-Path $audit 'smi-vid-patch.efi'
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne '09C45B4D540CA4CE5A03C56F3C2422C33273C2A4D68C985750CEC516482D6A23') { throw 'Source hash mismatch' }
    Copy-Item $source 'S:\EFI\FlowEcAudit\smi-vid-patch.efi'
    $hash = (Get-FileHash 'S:\EFI\FlowEcAudit\smi-vid-patch.efi' -Algorithm SHA256).Hash
    if ($hash -ne '09C45B4D540CA4CE5A03C56F3C2422C33273C2A4D68C985750CEC516482D6A23') { throw 'Diagnostic hash mismatch' }
    if (Test-Path 'S:\EFI\FlowEcAudit\smi-vid-patch.txt') { throw 'Unexpected existing log; archive before boot' }
    if (!(Test-Path 'S:\EFI\Microsoft\Boot\bootmgfw.efi')) { throw 'Windows boot manager missing' }
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-smi-vid-patch.txt') { throw 'Unexpected existing arm token' }
    [IO.File]::WriteAllText('S:\EFI\FlowEcAudit\arm-smi-vid-patch.txt', "GV301QE.415 APPLY VID PATCH TWO SECTORS V1`n", [Text.Encoding]::ASCII)
    $copy = Bcd @('/copy','{bootmgr}','/d','Flow SMI VID-only two-sector update (temporary)')
    $id = [regex]::Match($copy,'\{[0-9a-fA-F-]{36}\}').Value
    if (!$id) { throw "Cannot identify new entry: $copy" }
    $id | Set-Content $state
    Bcd @('/set',$id,'device','partition=S:') | Write-Output
    Bcd @('/set',$id,'path','\EFI\FlowEcAudit\smi-vid-patch.efi') | Write-Output
    # Keep the normal persistent boot order. Only BootNext selects this entry.
    Bcd @('/set','{fwbootmgr}','displayorder',$id,'/remove') | Write-Output
    Bcd @('/set','{fwbootmgr}','bootsequence',$id) | Write-Output
    Bcd @('/enum','firmware') | Write-Output
} catch {
    Remove-Item 'S:\EFI\FlowEcAudit\arm-smi-vid-patch.txt' -ErrorAction SilentlyContinue
    if ($id) {
        & bcdedit.exe /delete $id
        if ($LASTEXITCODE -eq 0) { Remove-Item $state -ErrorAction SilentlyContinue }
    }
    throw
} finally {
    & mountvol.exe S: /D
}
