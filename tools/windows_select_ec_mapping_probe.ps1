# Stages and arms the normal mapping read probe for one boot. Does not restart.
$ErrorActionPreference = 'Stop'
function Bcd([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return ($output -join "`n")
}
$audit = Join-Path $env:TEMP 'flowx13-ec-audit'
$state = Join-Path $audit 'mapping-probe-boot-entry.txt'
if (Test-Path $state) { throw 'Existing diagnostic entry state; inspect before proceeding' }
if ((Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Wrong BIOS' }
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint C:).ProtectionStatus -ne 'Off') { throw 'BitLocker protection is enabled' }
if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE') { throw 'Wrong model' }
$before = Bcd @('/enum','firmware')
if ($before -match '(?im)^bootsequence\s') { throw 'A one-time boot is already selected' }
$before | Set-Content (Join-Path $audit 'mapping-probe-firmware-before.txt')
if (Test-Path 'S:\') { throw 'S: occupied' }
& mountvol.exe S: /S
if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
$id = $null
try {
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus
    if (!($power | Where-Object PowerOnline)) { throw 'AC power not detected' }
    $source = Join-Path $audit 'mapping-probe.efi'
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne '43F99EAA7D65CE49F33B0F9319A964CFF93D5E6625C342634CD8E1922D8DC36B') { throw 'Source hash mismatch' }
    Copy-Item $source 'S:\EFI\FlowEcAudit\mapping-probe.efi'
    $hash = (Get-FileHash 'S:\EFI\FlowEcAudit\mapping-probe.efi' -Algorithm SHA256).Hash
    if ($hash -ne '43F99EAA7D65CE49F33B0F9319A964CFF93D5E6625C342634CD8E1922D8DC36B') { throw 'Diagnostic hash mismatch' }
    if (Test-Path 'S:\EFI\FlowEcAudit\mapping-probe.txt') { throw 'Unexpected existing log; archive before boot' }
    if (!(Test-Path 'S:\EFI\Microsoft\Boot\bootmgfw.efi')) { throw 'Windows boot manager missing' }
    if (Test-Path 'S:\EFI\FlowEcAudit\arm-mapping-probe.txt') { throw 'Unexpected existing arm token' }
    [IO.File]::WriteAllText('S:\EFI\FlowEcAudit\arm-mapping-probe.txt', "GV301QE.415 NORMAL MAPPING READ EC PROBE`n", [Text.Encoding]::ASCII)
    $copy = Bcd @('/copy','{bootmgr}','/d','Flow EC normal mapping read probe (temporary)')
    $id = [regex]::Match($copy,'\{[0-9a-fA-F-]{36}\}').Value
    if (!$id) { throw "Cannot identify new entry: $copy" }
    $id | Set-Content $state
    Bcd @('/set',$id,'device','partition=S:') | Write-Output
    Bcd @('/set',$id,'path','\EFI\FlowEcAudit\mapping-probe.efi') | Write-Output
    # Keep the normal persistent boot order. Only BootNext selects this entry.
    Bcd @('/set','{fwbootmgr}','displayorder',$id,'/remove') | Write-Output
    Bcd @('/set','{fwbootmgr}','bootsequence',$id) | Write-Output
    Bcd @('/enum','firmware') | Write-Output
} catch {
    Remove-Item 'S:\EFI\FlowEcAudit\arm-mapping-probe.txt' -ErrorAction SilentlyContinue
    if ($id) {
        & bcdedit.exe /delete $id
        if ($LASTEXITCODE -eq 0) { Remove-Item $state -ErrorAction SilentlyContinue }
    }
    throw
} finally {
    & mountvol.exe S: /D
}
