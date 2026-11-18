# Status-only diagnostic: no port writes, no data-port reads, no flash-mode entry.
param([Parameter(Mandatory=$true)][string]$ModulePath)
$ErrorActionPreference = 'Stop'
$model = (Get-CimInstance Win32_ComputerSystem).Model
$bios = (Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion
if ($model -notmatch 'GV301QE' -or $bios -ne 'GV301QE.415') {
    throw "Unexpected machine: $model / $bios"
}
$dll = Join-Path $env:ProgramFiles 'PawnIO\PawnIOLib.dll'
if (!(Test-Path $dll)) { throw "Missing library: $dll" }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class FlowEcStatus {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryW(string path);
    [DllImport("PawnIOLib.dll", CallingConvention=CallingConvention.Winapi)]
    static extern int pawnio_open(out IntPtr handle);
    [DllImport("PawnIOLib.dll", CallingConvention=CallingConvention.Winapi)]
    static extern int pawnio_load(IntPtr handle, byte[] blob, UIntPtr size);
    [DllImport("PawnIOLib.dll", CallingConvention=CallingConvention.Winapi)]
    static extern int pawnio_execute(IntPtr handle, [MarshalAs(UnmanagedType.LPStr)] string name,
        ulong[] input, UIntPtr inputSize, ulong[] output, UIntPtr outputSize, out UIntPtr returned);
    [DllImport("PawnIOLib.dll", CallingConvention=CallingConvention.Winapi)]
    static extern int pawnio_close(IntPtr handle);
    static void Check(int hr) { Marshal.ThrowExceptionForHR(hr); }
    public static ulong[] Read(byte[] module) {
        IntPtr h = IntPtr.Zero;
        using (var mutex = new Mutex(false, @"Global\Access_EC")) {
            bool locked = false;
            try {
                try { locked = mutex.WaitOne(2000); }
                catch (AbandonedMutexException) { locked = true; }
                if (!locked) throw new Exception("EC mutex busy");
                Check(pawnio_open(out h));
                Check(pawnio_load(h, module, (UIntPtr)module.Length));
                ulong[] samples = new ulong[8];
                for (int i = 0; i < samples.Length; i++) {
                    ulong[] output = new ulong[1]; UIntPtr returned;
                    // Reading STATUS does not consume pending ACPI response data.
                    Check(pawnio_execute(h, "ioctl_pio_read", new ulong[]{0x66},
                        (UIntPtr)1, output, (UIntPtr)1, out returned));
                    if (returned.ToUInt64() != 1 || output[0] > 255)
                        throw new Exception("Invalid port read result");
                    samples[i] = output[0];
                    Thread.Sleep(10);
                }
                return samples;
            } finally {
                if (h != IntPtr.Zero) pawnio_close(h);
                if (locked) mutex.ReleaseMutex();
            }
        }
    }
}
'@
if ([FlowEcStatus]::LoadLibraryW($dll) -eq [IntPtr]::Zero) { throw 'Library load failed' }
$samples = [FlowEcStatus]::Read([IO.File]::ReadAllBytes((Resolve-Path $ModulePath)))
[ordered]@{ Model=$model; BIOS=$bios; Port='0x66'; StatusSamples=@($samples);
    PortWrites=0; FlashModeEntered=$false } | ConvertTo-Json
