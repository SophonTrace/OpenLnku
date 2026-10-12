param([string]$Executable = '', [string]$Icon = '')
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Executable) { $Executable = Join-Path $workspace 'build/OpenLnku.exe' }
if (-not $Icon) { $Icon = Join-Path $workspace 'assets/branding/app.ico' }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class OpenLnkuIconResources {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr library);
    [DllImport("kernel32.dll")] static extern IntPtr FindResource(IntPtr library,IntPtr name,IntPtr type);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr library,IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr library,IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr resource);
    static byte[] Read(IntPtr library,int type,int id) {
        var resource=FindResource(library,new IntPtr(id),new IntPtr(type));
        if(resource==IntPtr.Zero) throw new Exception("Missing icon resource");
        var bytes=new byte[checked((int)SizeofResource(library,resource))];
        Marshal.Copy(LockResource(LoadResource(library,resource)),bytes,0,bytes.Length); return bytes;
    }
    public static int Verify(string exe,string icon) {
        var source=File.ReadAllBytes(icon);
        var library=LoadLibraryEx(exe,IntPtr.Zero,2); // read resources, never execute the file
        if(library==IntPtr.Zero) throw new Exception("Cannot read executable resources");
        try {
            var group=Read(library,14,1);
            int count=BitConverter.ToUInt16(source,4);
            if(count!=9 || BitConverter.ToUInt16(group,4)!=count) throw new Exception("Wrong icon layer count");
            for(int i=0;i<count;i++) {
                int original=6+16*i, embedded=6+14*i;
                for(int j=0;j<12;j++) if(source[original+j]!=group[embedded+j]) throw new Exception("Icon metadata mismatch");
                int length=checked((int)BitConverter.ToUInt32(source,original+8));
                int offset=checked((int)BitConverter.ToUInt32(source,original+12));
                int id=BitConverter.ToUInt16(group,embedded+12);
                var actual=Read(library,3,id);
                if(actual.Length!=length) throw new Exception("Icon size mismatch");
                for(int j=0;j<length;j++) if(source[offset+j]!=actual[j]) throw new Exception("Embedded icon differs from current brand");
            }
            return count;
        } finally { FreeLibrary(library); }
    }
}
'@
$layers = [OpenLnkuIconResources]::Verify((Resolve-Path -LiteralPath $Executable).Path, (Resolve-Path -LiteralPath $Icon).Path)
Write-Output "EXE icon verified: all $layers embedded layers match the current brand ICO."
