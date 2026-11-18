# EXPERIMENTAL: DESTRUCTIVE erase and restoration of stock sector 03D000.
# Stages and arms the stock SMI stock sector erase/restore probe for one boot. Does not restart.
$ErrorActionPreference = 'Stop'
function Bcd([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return ($output -join "`n")
}
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$state = Join-Path $audit 'smi-stock-restore-boot-entry.txt'
if (Test-Path $state) { throw 'Existing diagnostic entry state; inspect before proceeding' }
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint C:).ProtectionStatus -ne 'Off') { throw 'BitLocker protection is enabled' }
if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE') { throw 'Wrong model' }
$before = Bcd @('/enum','firmware')
if ($before -match '(?im)^bootsequence\s') { throw 'A one-time boot is already selected' }
$before | Set-Content (Join-Path $audit 'smi-stock-restore-firmware-before.txt')
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
$id = $null
try {
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus
    if (!($power | Where-Object PowerOnline)) { throw 'AC power not detected' }
    if ((Get-CimInstance Win32_Battery).EstimatedChargeRemaining -lt 80) { throw 'Battery below 80 percent' }
    if ((Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash -ne '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511') { throw 'Windows boot manager hash mismatch' }
    $source = Join-Path $audit 'smi-stock-restore.efi'
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne 'C0437BDB01D8339A98D1BB6236D4EA9F5B8FF6E5047D30D9245524E1D43A7EB2') { throw 'Source hash mismatch' }
    Copy-Item $source 'S:\EFI\FlowEcAudit\smi-stock-restore.efi'
    $hash = (Get-FileHash 'S:\EFI\FlowEcAudit\smi-stock-restore.efi' -Algorithm SHA256).Hash
    if ($hash -ne 'C0437BDB01D8339A98D1BB6236D4EA9F5B8FF6E5047D30D9245524E1D43A7EB2') { throw 'Diagnostic hash mismatch' }
    if (Test-Path 'S:\EFI\FlowEcAudit\smi-stock-restore.txt') { throw 'Unexpected existing log; archive before boot' }
    if (!(Test-Path 'S:\EFI\Microsoft\Boot\bootmgfw.efi')) { throw 'Windows boot manager missing' }
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-smi-stock-restore.txt') { throw 'Unexpected existing arm token' }
    [IO.File]::WriteAllText('S:\EFI\FlowEcAudit\arm-smi-stock-restore.txt', "GV301QE.415 HOST SPI ERASE RESTORE STOCK 03D000 V1`n", [Text.Encoding]::ASCII)
    $copy = Bcd @('/copy','{bootmgr}','/d','Flow SMI stock sector erase/restore (temporary)')
    $id = [regex]::Match($copy,'\{[0-9a-fA-F-]{36}\}').Value
    if (!$id) { throw "Cannot identify new entry: $copy" }
    $id | Set-Content $state
    Bcd @('/set',$id,'device','partition=S:') | Write-Output
    Bcd @('/set',$id,'path','\EFI\FlowEcAudit\smi-stock-restore.efi') | Write-Output
    # Keep the normal persistent boot order. Only BootNext selects this entry.
    Bcd @('/set','{fwbootmgr}','displayorder',$id,'/remove') | Write-Output
    Bcd @('/set','{fwbootmgr}','bootsequence',$id) | Write-Output
    Bcd @('/enum','firmware') | Write-Output
} catch {
    Remove-Item 'S:\EFI\FlowEcAudit\arm-smi-stock-restore.txt' -ErrorAction SilentlyContinue
    if ($id) {
        & bcdedit.exe /delete $id
        if ($LASTEXITCODE -eq 0) { Remove-Item $state -ErrorAction SilentlyContinue }
    }
    throw
} finally {
    & mountvol.exe S: /D
}
