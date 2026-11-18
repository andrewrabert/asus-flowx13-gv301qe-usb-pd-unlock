#Requires -Version 5.1
[CmdletBinding()]
param([switch]$Cancel)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The payload contains the unchanged, hardware-tested EFI binaries.
$payloadHash = 'E9B5F7393125A93DB460EFF9D6C90ACC9B3F8F3F9C707C2AD1433B51E004107A'
$manifestHash = '1D9003327ACF9714C18CC1C757F78431417E57BE9C879AA972743F38E8531169'
$root = $PSScriptRoot
$work = Join-Path $root 'work'
$runtime = Join-Path $work 'runtime'
$backups = Join-Path $work 'backups'
$principal = $null

function Invoke-Step([string]$Action) {
    & (Join-Path $runtime 'engine.ps1') -Action $Action *>> (Join-Path $work 'session.log')
}
function Done([string]$Stem) {
    Test-Path (Join-Path $backups "$Stem-boot-entry-completed.txt")
}
function Next-Stage {
    # Determine progress from collected results, not an easily stale step counter.
    if (!(Done 'host-backup')) { return 'BackupHost' }
    if (!(Done 'backup-probe') -and !(Test-Path (Join-Path $backups 'original-ec'))) { return 'BackupEC' }
    if (!(Done 'smi-vid-patch')) { return 'Flash' }
    if (!(Done 'smi-vid-verify')) { return 'VerifyHost' }
    if (!(Test-Path (Join-Path $backups 'original-ec'))) { return 'Reset' }
    if (!(Done 'backup-probe')) { return 'VerifyEC' }
    return 'Complete'
}
function Get-Collector([string]$Stem) {
    switch ($Stem) {
        'host-backup' { return 'CollectHost' }
        'backup-probe' {
            if (Test-Path (Join-Path $backups 'original-ec')) { return 'CollectVerifyEC' }
            return 'CollectEC'
        }
        'smi-vid-patch' { return 'CollectFlash' }
        'smi-vid-verify' { return 'CollectVerifyHost' }
        default { throw 'Unknown pending operation' }
    }
}
function Stage-And-Restart([string]$Stage) {
    Invoke-Step $Stage
    Write-Host "`nSave your work. This operation is selected for the next boot."
    if ((Read-Host 'Restart now? Type YES (otherwise run this launcher again after restarting)') -eq 'YES') {
        Restart-Computer
    } else {
        Write-Host 'Restart manually when ready. To cancel before restarting: .\flash.ps1 -Cancel'
    }
}

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
        if ($Cancel) { $arguments += ' -Cancel' }
        $child = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $child.ExitCode
    }
    if ((Get-FileHash (Join-Path $root 'payload.zip') -Algorithm SHA256).Hash -ne $payloadHash) {
        throw 'payload.zip is missing or changed. Download a fresh complete copy.'
    }
    if (!(Test-Path $runtime)) {
        New-Item -ItemType Directory -Force $work | Out-Null
        $unpacked = Join-Path $work ('unpack-' + [guid]::NewGuid().ToString())
        Expand-Archive -LiteralPath (Join-Path $root 'payload.zip') -DestinationPath $unpacked
        Move-Item $unpacked $runtime
    }
    $manifestPath = Join-Path $runtime 'SHA256.json'
    if ((Get-FileHash $manifestPath -Algorithm SHA256).Hash -ne $manifestHash) { throw 'Extracted manifest changed' }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    foreach ($item in $manifest.PSObject.Properties) {
        if ((Get-FileHash (Join-Path $runtime $item.Name) -Algorithm SHA256).Hash -ne $item.Value) {
            throw "Extracted file changed: $($item.Name)"
        }
    }
    Write-Host "`nGV301QE / BIOS 415 experimental vendor-check patch"
    Write-Host 'Keep ASUS power connected. Reopen flash.cmd after each Windows restart.'
    Write-Host "Backups and logs: $backups`n"
    if ($Cancel) { Invoke-Step 'Cancel'; return }
    if (!(Test-Path (Join-Path $backups 'bootmgr.sha256'))) {
        Write-Host 'This writes firmware. Failure can require an external programmer.'
        Write-Host 'The five-byte patch is verified; unrestricted charging is NOT established.'
        Write-Host 'Requires exact GV301QE.415, Secure Boot off, BitLocker protection off, battery >=80%.'
        if ((Read-Host 'Begin backups and guided installation? Type YES') -ne 'YES') { return }
        Invoke-Step 'Init'
    }
    $pending = @(Get-ChildItem $backups -Filter '*-boot-entry.txt')
    if ($pending.Count -gt 1) { throw 'Multiple pending entries. Preserve work and investigate.' }
    if ($pending.Count -eq 1) {
        $stem = $pending[0].Name -replace '-boot-entry.txt$',''
        $collect = Get-Collector $stem
        Write-Host 'Checking the previous boot. If you have not restarted yet, restart before continuing.'
        Invoke-Step $collect
    }
    $next = Next-Stage
    switch ($next) {
        'BackupHost' { Write-Host 'Next: save two full BIOS backups.'; Stage-And-Restart $next }
        'BackupEC' { Write-Host 'Next: save two internal EC backups (about 6-7 minutes).'; Stage-And-Restart $next }
        'Flash' {
            Write-Host "Copy $backups to another computer or external drive now."
            Write-Host 'Next: apply the patch. Keep power connected and do not interrupt.'
            if ((Read-Host 'Backups saved elsewhere and ready to flash? Type FLASH') -ne 'FLASH') { return }
            Stage-And-Restart $next
        }
        'VerifyHost' { Write-Host 'Next: verify the patched BIOS survived a restart.'; Stage-And-Restart $next }
        'Reset' {
            Write-Host 'The host patch is verified. A physical EC reset is now required:'
            Write-Host 'Shut down, remove external peripherals, leave the adapter connected.'
            Write-Host 'Hold power for 40 seconds, release, then turn on and reopen flash.cmd.'
            Write-Host 'For the ASUS 20-second variant: release after the indicator finishes flashing.'
            if ((Read-Host 'If you have ALREADY completed that reset, type RESET; otherwise press Enter and do it now') -ne 'RESET') { return }
            Write-Host 'Next: read back the internal EC (about 6-7 minutes).'
            Stage-And-Restart 'VerifyEC'
        }
        'VerifyEC' { Stage-And-Restart $next }
        'Complete' {
            Invoke-Step 'CheckComplete'
            Write-Host "`nDONE: the exact five-byte patch is verified in the host BIOS and internal EC."
            Write-Host 'Keep your backups. Restore your previous Secure Boot/BitLocker settings.'
            Write-Host 'Charging performance still needs separate testing. Do not reflash.'
        }
    }
} catch {
    Write-Host "`nSTOPPED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Preserve work and any screen messages. Do not delete state or bypass checks.'
    exit 1
} finally {
    if ($principal -and $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Read-Host 'Press Enter to close' | Out-Null
    }
}
