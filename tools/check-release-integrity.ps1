#requires -Version 7.4
param([string]$Executable = (Join-Path $PSScriptRoot '..\build\OpenNex.exe'), [switch]$RequireNormal)
$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Executable).Path
# Read only. Never change workspace ACLs or elevate the application.
$labels = & "$env:WINDIR\System32\icacls.exe" $resolved 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Unable to inspect executable label: $labels" }
$low = $labels -match 'Low Mandatory Level|低强制级别|S-1-16-4096'
$result = [ordered]@{
    executable = $resolved
    lowIntegrityLabel = $low
    inspectedUtc = [DateTime]::UtcNow.ToString('o')
    acl = $labels.Trim()
    distribution = if ($low) { 'Restore the executable label from a normal user shell, or extract the release ZIP into a new normal user directory before launch.' } else { 'Executable has no Low mandatory label. Launch from Windows Explorer or another normal user process.' }
}
$result | ConvertTo-Json -Depth 3
if ($RequireNormal -and $low) { throw 'Release staging executable is Low integrity. Extract the ZIP into a normal directory and check the extracted executable before native acceptance.' }
