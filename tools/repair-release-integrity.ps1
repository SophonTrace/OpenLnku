#requires -Version 7.4
param([switch]$IncludePrevious, [switch]$FixtureOnly, [switch]$BuildOnly, [switch]$ReleaseOnly, [switch]$AcceptanceDirectories, [switch]$ToolchainOnly, [string]$BrandAcceptanceRoot)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ((Get-Item -LiteralPath $taskRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'The repair workspace must not be a reparse point.' }
$level = & (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')
if ($level.integrityRID -lt 8192) { throw 'Run this repair from a normal user PowerShell. A Low-integrity process cannot raise a file label.' }
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
if ($BrandAcceptanceRoot) {
    $brandPath = [IO.Path]::GetFullPath($BrandAcceptanceRoot)
    $brandParent = Join-Path $taskRoot '.deps'
    $brandName = [IO.Path]::GetFileName($brandPath)
    if ([IO.Path]::GetDirectoryName($brandPath) -ne $brandParent -or $brandName -notmatch '^brand-migration-([0-9a-f]{32})$') { throw 'Unsafe brand acceptance root.' }
    $brandId = $Matches[1]
    if ((Get-Content -LiteralPath (Join-Path $brandPath '.brand-migration-owned') -Raw).Trim() -ne $brandId) { throw 'Brand acceptance ownership marker mismatch.' }
}
$directoryRepair = $AcceptanceDirectories -or [bool]$BrandAcceptanceRoot
$relativeTargets = if ($BrandAcceptanceRoot) { @([IO.Path]::GetRelativePath($taskRoot,$brandPath)) }
elseif ($AcceptanceDirectories) { @('.deps\release-acceptance', '.deps\release-acceptance\nsis-temp') }
elseif ($ToolchainOnly) { @('.deps\NSIS\nsis-3.13\Bin\makensis.exe') }
elseif ($FixtureOnly) { @('build\settings_fixture.exe') }
elseif ($BuildOnly) { @('build\OpenLnku.exe', 'build\settings_fixture.exe', 'build\speed_probe.exe', 'build\localization_tests.exe') }
elseif ($ReleaseOnly) { @('build\OpenLnku.exe', 'build\settings_fixture.exe', "dist\OpenLnku-$version-windows-x64\OpenLnku.exe", "dist\OpenLnku-$version-setup-x64.exe") }
else { @(
    'build\OpenLnku.exe', 'build\settings_fixture.exe',
    "dist\OpenLnku-$version-windows-x64\OpenLnku.exe", "dist\OpenLnku-$version-setup-x64.exe",
    '.deps\release-acceptance\setup-old.exe', '.deps\release-acceptance\setup-new.exe',
    '.deps\release-file-acceptance\setup-old.exe', '.deps\release-file-acceptance\setup-new.exe'
) }
if ($IncludePrevious -and -not ($FixtureOnly -or $BuildOnly -or $ReleaseOnly -or $AcceptanceDirectories -or $ToolchainOnly)) { $relativeTargets += @('dist\OpenLnku-1.0.0-windows-x64\OpenLnku.exe', 'dist\OpenLnku-1.0.0-setup-x64.exe') }
$userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$accessSection = [Security.AccessControl.AccessControlSections]::Access
$report = @()
foreach ($relative in $relativeTargets) {
    $target = [IO.Path]::GetFullPath((Join-Path $taskRoot $relative))
    if (-not $target.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe repair path.' }
    $parentCheck = [IO.Path]::GetDirectoryName($target)
    while ($parentCheck -and $parentCheck -ne $taskRoot) {
        if (Test-Path -LiteralPath $parentCheck) {
            if ((Get-Item -LiteralPath $parentCheck).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed in repair ancestors.' }
        }
        $parentCheck = [IO.Path]::GetDirectoryName($parentCheck)
    }
    if ($AcceptanceDirectories) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskRoot '.deps\release-acceptance\package-old\.acceptance-owned') -PathType Leaf)) { throw 'Acceptance directory lacks its owned fixture marker.' }
        New-Item -ItemType Directory -Path $target -Force | Out-Null
    }
    if (-not (Test-Path -LiteralPath $target)) { continue }
    $node = Get-Item -LiteralPath $target
    while ($node -and $node.FullName -ne $taskRoot) {
        if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed in repair paths.' }
        $node = if ($node -is [IO.FileInfo]) { $node.Directory } else { $node.Parent }
    }
    $hashBefore = if ($directoryRepair) { $null } else { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash }
    $acl = Get-Acl -LiteralPath $target
    $originalAccess = $acl.GetSecurityDescriptorSddlForm($accessSection)
    $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier])
    if ($owner -ne $userSid) { throw "Refusing to repair a file owned by another user: $target" }
    try {
        # LABEL_SECURITY_INFORMATION requires WRITE_OWNER, absent from a Modify-only grant.
        # Grant only this right to the existing owner, for only the enumerated file.
        $rule = [Security.AccessControl.FileSystemAccessRule]::new($userSid, [Security.AccessControl.FileSystemRights]::TakeOwnership, [Security.AccessControl.AccessControlType]::Allow)
        $acl.AddAccessRule($rule)
        Set-Acl -LiteralPath $target -AclObject $acl
        $label = if ($directoryRepair) { '(OI)(CI)M' } else { 'M' }
        $labelOutput = & "$env:WINDIR\System32\icacls.exe" $target /setintegritylevel $label 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "Unable to set Medium label: $labelOutput" }
    } finally {
        $restore = Get-Acl -LiteralPath $target
        $restore.SetSecurityDescriptorSddlForm($originalAccess, $accessSection)
        Set-Acl -LiteralPath $target -AclObject $restore
    }
    $afterAccess = (Get-Acl -LiteralPath $target).GetSecurityDescriptorSddlForm($accessSection)
    $hashAfter = if ($directoryRepair) { $null } else { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash }
    if ($originalAccess -ne $afterAccess -or $hashBefore -ne $hashAfter) { throw "File content or original access rules changed: $target" }
    & (Join-Path $PSScriptRoot 'check-release-integrity.ps1') -Executable $target -RequireNormal | Out-Null
    $report += [pscustomobject]@{ file = $relative; integrity = 'Medium'; originalAccessRestored = $true; sha256 = $hashAfter }
}
$report | ConvertTo-Json -Depth 4
