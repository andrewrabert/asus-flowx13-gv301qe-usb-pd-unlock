# Offline state-machine tests. Loads function declarations only; never runs the launcher.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot -Parent
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'flash-kit/flash.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($node in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    Invoke-Expression $node.Extent.Text
}
$backups = Join-Path ([IO.Path]::GetTempPath()) ('flow-kit-test-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory $backups | Out-Null
$script:count = 0
function Expect($Actual, $Expected) {
    if ($Actual -ne $Expected) { throw "Expected $Expected, got $Actual" }
    $script:count++
}
function Mark([string]$Stem) { '' | Set-Content (Join-Path $backups "$Stem-boot-entry-completed.txt") }
try {
    Expect (Next-Stage) 'BackupHost'
    Expect (Get-Collector 'host-backup') 'CollectHost'
    Mark 'host-backup'
    Expect (Next-Stage) 'BackupEC'
    Expect (Get-Collector 'backup-probe') 'CollectEC'
    Mark 'backup-probe'
    Expect (Next-Stage) 'Flash'
    Expect (Get-Collector 'smi-vid-patch') 'CollectFlash'
    Mark 'smi-vid-patch'
    Expect (Next-Stage) 'VerifyHost'
    Expect (Get-Collector 'smi-vid-verify') 'CollectVerifyHost'
    Mark 'smi-vid-verify'
    Expect (Next-Stage) 'Reset'
    # Reset stage remains pending across launches until backup archival occurs.
    Expect (Next-Stage) 'Reset'
    New-Item -ItemType Directory (Join-Path $backups 'original-ec') | Out-Null
    Move-Item (Join-Path $backups 'backup-probe-boot-entry-completed.txt') (Join-Path $backups 'original-ec')
    Expect (Next-Stage) 'VerifyEC'
    Expect (Get-Collector 'backup-probe') 'CollectVerifyEC'
    # Cancelling a readback leaves it available, without repeating a flash.
    Expect (Next-Stage) 'VerifyEC'
    Mark 'backup-probe'
    Expect (Next-Stage) 'Complete'
    Expect (Next-Stage) 'Complete'
    $rejected = $false
    try { Get-Collector 'unknown' } catch { $rejected = $true }
    Expect $rejected $true
    Write-Output "$script:count launcher state checks passed; no hardware access"
} finally { Remove-Item $backups -Recurse -Force }
