#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('Init','BackupHost','CollectHost','BackupEC','CollectEC','Flash','CollectFlash','VerifyHost','CollectVerifyHost','VerifyEC','CollectVerifyEC','Cancel')][string]$Action)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$work = Join-Path $PSScriptRoot 'work'
$esp = 'S:\EFI\FlowEcAudit'
$stockEC = '5BC14FB3C61C9FA346210D18C67D45C93B7EF020E7D1C06369FE1D92A5FB7478'
$patchedEC = 'EB73DE50E65E67915EE7474203DC2FBC3DD055A635157C2DE82457299DFC957E'

function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Require-Log([string]$Name, [string[]]$Patterns) {
    $text = Get-Content (Join-Path $work $Name) -Raw
    foreach ($pattern in $Patterns) {
        if ($text -notmatch $pattern) { throw "Unsuccessful/incomplete log $Name; missing $pattern" }
    }
}
function Check-Pair([string]$Directory, [string]$Prefix, [long]$Size, [string]$Expected = '') {
    $a = Join-Path $Directory "$Prefix-pass0.bin"
    $b = Join-Path $Directory "$Prefix-pass1.bin"
    if ((Get-Item $a).Length -ne $Size -or (Get-Item $b).Length -ne $Size) { throw 'Wrong backup size' }
    $hash = Hash $a
    if ($hash -ne (Hash $b)) { throw 'Backup passes disagree' }
    if ($Expected -and $hash -ne $Expected) { throw 'Unexpected EC image; do not flash or repeat the updater' }
}
function Check-Host {
    Require-Log 'ec-host-backup-collected.txt' @('(?m)^backup_result=0 completed_passes=2 bytes_per_pass=16777216\b')
    Check-Pair $work 'host-backup' 16777216
    $hostBytes = [IO.File]::ReadAllBytes((Join-Path $work 'host-backup-pass0.bin'))
    $original = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'payload\original-sectors.bin'))
    foreach ($n in 0,1) {
        $offset = @(0x3D000,0x42000)[$n]
        for ($i=0; $i -lt 4096; $i++) {
            if ($hostBytes[$offset+$i] -ne $original[4096*$n+$i]) { throw 'Host sector differs from tested original. Already patched or incompatible; refusing.' }
        }
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $ecHash = [BitConverter]::ToString($sha.ComputeHash($hostBytes,0x31000,196608)).Replace('-','') }
    finally { $sha.Dispose() }
    if ($ecHash -ne $stockEC) { throw 'Host EC image is not the tested stock image' }
}
function Check-EC([string]$Directory,[string]$Expected) {
    $text = Get-Content (Join-Path $Directory 'ec-backup-probe-collected.txt') -Raw
    if ($text -notmatch '(?m)^result=0 completed_passes=2 window=196608\b' -or
        $text -notmatch 'chip=51,25,02,00,00,00\b' -or
        $text -notmatch 'last_result=0 incomplete=0 dirty=0 restored=1\b') { throw 'EC backup did not complete safely on the expected chip' }
    Check-Pair $Directory 'ec-backup' 196608 $Expected
}
function Check-Flash {
    Require-Log 'ec-smi-vid-patch-collected.txt' @('smi_access_result=0 requests=2 enable_status=00 disable_status=00\b','vid_patch_result=0 commands=\d+ restored=1 dirty_mask=0 erases=2 programs=256\b','exclusion_verified=1\b','cold_reset=0 buffer_guard_failed=0\b')
}
function Check-Verify {
    Require-Log 'ec-smi-vid-verify-collected.txt' @('smi_access_result=0 requests=2 enable_status=00 disable_status=00\b','vid_verify_result=0 commands=\d+ restored=1 dirty_mask=0 erases=0 programs=0\b','exclusion_verified=1\b','cold_reset=0 buffer_guard_failed=0\b')
}
function Mount-ESP {
    if (Test-Path 'S:\') { throw 'S: is occupied; free it before continuing' }
    & mountvol.exe S: /S
    if ($LASTEXITCODE -ne 0) { throw 'ESP mount failed' }
}
function Run([string]$Script) { & (Join-Path $PSScriptRoot "scripts\$Script.ps1") }
function No-Pending {
    if (@(Get-ChildItem $work -Filter '*-boot-entry.txt').Count) { throw 'An operation is pending. Collect its result before staging another.' }
}

# Manifest is a corruption check, not a signature or trust anchor.
$manifest = Get-Content (Join-Path $PSScriptRoot 'SHA256.json') -Raw | ConvertFrom-Json
foreach ($item in $manifest.PSObject.Properties) {
    if ((Hash (Join-Path $PSScriptRoot $item.Name)) -ne $item.Value) { throw "Package integrity check failed: $($item.Name)" }
}
if ($Action -eq 'Init') {
    if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'ROG Flow X13 GV301QE' -or
        (Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion -ne 'GV301QE.415') { throw 'Only ROG Flow X13 GV301QE, BIOS GV301QE.415 is supported' }
    if (Test-Path $work) { throw 'work already exists; preserve it and inspect its state instead of reinitializing' }
    Mount-ESP
    try {
        if (Test-Path $esp) { throw 'Existing EFI\FlowEcAudit directory. Preserve and review previous diagnostics before using this kit.' }
        New-Item -ItemType Directory $work | Out-Null
        Hash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' | Set-Content (Join-Path $work 'bootmgr.sha256')
        Copy-Item (Join-Path $PSScriptRoot 'bin\*.efi') $work
        New-Item -ItemType Directory $esp | Out-Null
    } finally { & mountvol.exe S: /D }
    Write-Host 'Initialized. Next: BackupHost. No boot entry armed.'
    return
}
if (!(Test-Path (Join-Path $work 'bootmgr.sha256'))) { throw 'Run Init first' }
switch ($Action) {
    'Cancel' {
        $pending = @(Get-ChildItem $work -Filter '*-boot-entry.txt')
        if ($pending.Count -ne 1) { throw 'Expected exactly one pending operation; inspect state manually' }
        $stem = $pending[0].Name -replace '-boot-entry.txt$',''
        if ($stem -notin @('host-backup','backup-probe','smi-vid-patch','smi-vid-verify')) { throw 'Unrecognized pending operation' }
        $id = (Get-Content $pending[0].FullName -Raw).Trim()
        if ($id -notmatch '^\{[0-9a-fA-F-]{36}\}$') { throw 'Invalid entry ID' }
        $entry = (& bcdedit.exe /enum $id 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $entry -notmatch [regex]::Escape("\EFI\FlowEcAudit\$stem.efi")) { throw 'Entry differs; refusing cancellation' }
        Mount-ESP
        try {
            if (Test-Path (Join-Path $esp "$stem.txt")) { throw 'Operation has a log; collect/investigate its result instead of cancelling' }
            if (!(Test-Path (Join-Path $esp "arm-$stem.txt"))) { throw 'Arm token missing; operation may have started' }
            & bcdedit.exe /delete $id
            if ($LASTEXITCODE -ne 0) { throw 'Entry deletion failed' }
            Remove-Item (Join-Path $esp "arm-$stem.txt")
            Move-Item $pending[0].FullName (Join-Path $work ("$stem-cancelled-" + [guid]::NewGuid().ToString() + '.txt'))
        } finally { & mountvol.exe S: /D }
        Write-Host 'Cancelled the pending temporary boot entry. No firmware access performed.'
    }
    'BackupHost' { No-Pending; Run 'windows_select_host_backup' }
    'CollectHost' { Run 'windows_collect_host_backup'; Check-Host; Write-Host 'Host backup verified. Copy work to external storage before Flash.' }
    'BackupEC' { No-Pending; Check-Host; Run 'windows_select_ec_backup_probe' }
    'CollectEC' { Run 'windows_collect_ec_backup_probe'; Check-EC $work $stockEC; Write-Host 'Original EC backup verified.' }
    'Flash' {
        No-Pending
        Check-Host
        Check-EC $work $stockEC
        if (Test-Path (Join-Path $work 'smi-vid-patch-boot-entry-completed.txt')) { throw 'Updater already ran; do not repeat it' }
        Run 'windows_select_smi_vid_patch'
    }
    'CollectFlash' { Run 'windows_collect_smi_vid_patch'; Check-Flash; Write-Host 'Host patch verified. Next: VerifyHost.' }
    'VerifyHost' { No-Pending; Check-Flash; Run 'windows_select_smi_vid_verify' }
    'CollectVerifyHost' { Run 'windows_collect_smi_vid_verify'; Check-Verify; Write-Host 'Host patch persisted. Perform the physical EC reset in README, then VerifyEC.' }
    'VerifyEC' {
        No-Pending
        Check-Flash
        Check-Verify
        $archive = Join-Path $work 'original-ec'
        # Archive stock EC readouts once so the unchanged reader can use its fixed filenames.
        if (!(Test-Path $archive)) {
            Check-EC $work $stockEC
            Mount-ESP
            try {
                if (Test-Path (Join-Path $esp 'arm-backup-probe.txt')) { throw 'Unconsumed backup token' }
                Check-Pair $esp 'ec-backup' 196608 $stockEC
                if (Test-Path (Join-Path $esp 'original-ec')) { throw 'Existing ESP archive; inspect before proceeding' }
                New-Item -ItemType Directory (Join-Path $esp 'original-ec') | Out-Null
                foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin','backup-probe.txt')) {
                    Move-Item (Join-Path $esp $name) (Join-Path $esp "original-ec\$name")
                }
                New-Item -ItemType Directory $archive | Out-Null
                foreach ($name in @('ec-backup-pass0.bin','ec-backup-pass1.bin','ec-backup-probe-collected.txt','backup-probe-boot-entry-completed.txt')) {
                    Move-Item (Join-Path $work $name) (Join-Path $archive $name)
                }
            } finally { & mountvol.exe S: /D }
        }
        Check-EC $archive $stockEC
        Run 'windows_select_ec_backup_probe'
    }
    'CollectVerifyEC' { Run 'windows_collect_ec_backup_probe'; Check-EC $work $patchedEC; Write-Host 'SUCCESS: both full internal EC readouts match the five-byte patch. Charging behavior still requires testing.' }
}
if ($Action -in @('BackupHost','BackupEC','Flash','VerifyHost','VerifyEC')) {
    Write-Host 'One boot armed. Close applications, then use Windows Restart. After Windows returns, run the matching Collect action.'
}
