param([int]$ProcessId = $PID)
$ErrorActionPreference = 'Stop'
if (-not ('OpenLnkuProcessIntegrity' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class OpenLnkuProcessIntegrity {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int kind, IntPtr buffer, uint size, out uint required);
    [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
    [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
    public static uint Read(uint id) {
        IntPtr process = OpenProcess(0x1000, false, id);
        if(process == IntPtr.Zero) throw new Win32Exception();
        IntPtr token = IntPtr.Zero;
        try {
            if(!OpenProcessToken(process, 8, out token)) throw new Win32Exception();
            uint size;
            GetTokenInformation(token, 25, IntPtr.Zero, 0, out size);
            IntPtr buffer = Marshal.AllocHGlobal((int)size);
            try {
                if(!GetTokenInformation(token, 25, buffer, size, out size)) throw new Win32Exception();
                IntPtr sid = Marshal.ReadIntPtr(buffer);
                byte count = Marshal.ReadByte(GetSidSubAuthorityCount(sid));
                return (uint)Marshal.ReadInt32(GetSidSubAuthority(sid, (uint)(count - 1)));
            } finally { Marshal.FreeHGlobal(buffer); }
        } finally { if(token != IntPtr.Zero) CloseHandle(token); CloseHandle(process); }
    }
}
'@
}
$level = [OpenLnkuProcessIntegrity]::Read([uint32]$ProcessId)
[pscustomobject]@{ processId = $ProcessId; integrityRID = $level; level = $(if ($level -lt 8192) { 'Low' } elseif ($level -lt 12288) { 'Medium' } else { 'High or System' }) }
