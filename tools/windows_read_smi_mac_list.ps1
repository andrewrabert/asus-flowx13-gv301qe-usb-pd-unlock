# Reads only the variable-name list used by stock SmiFlash backup/restore.
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FlowFirmwareRead {
 [DllImport("ntdll.dll")] public static extern int RtlAdjustPrivilege(uint privilege, bool enable, bool currentThread, out bool old);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern uint GetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, out uint attributes);
}
'@
$old=$false
$status=[FlowFirmwareRead]::RtlAdjustPrivilege(22,$true,$false,[ref]$old)
if ($status -ne 0) { throw "SeSystemEnvironmentPrivilege failed: $status" }
try {
 $b=New-Object byte[] 65536
 $attr=[uint32]0
 $n=[FlowFirmwareRead]::GetFirmwareEnvironmentVariableExW('MACNamesListVar','{f70ebfe2-7bad-4683-8daf-00ad2125069d}',$b,$b.Length,[ref]$attr)
 $errorCode=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 [ordered]@{Name='MACNamesListVar';Bytes=$n;Attributes=$attr;Win32Error=$(if($n){0}else{$errorCode});Missing=($n -eq 0 -and $errorCode -eq 203)} | ConvertTo-Json
} finally {
 $ignored=$false
 [void][FlowFirmwareRead]::RtlAdjustPrivilege(22,$old,$false,[ref]$ignored)
}
