#requires -Version 7.4
param([switch]$FileOnly, [ValidatePattern('^\d+\.\d+\.\d+$')][string]$OldVersion = '0.1.1')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testName = if ($FileOnly) { 'release-file-acceptance' } else { 'release-acceptance' }
$testRoot = Join-Path $taskRoot ('.deps\' + $testName)
$resolvedTest = [IO.Path]::GetFullPath($testRoot)
if (-not $resolvedTest.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test root.' }
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$oldPackage = Join-Path $testRoot 'package-old'
if (Test-Path $oldPackage) {
    $resolvedOld = (Resolve-Path -LiteralPath $oldPackage).Path
    if (-not $resolvedOld.StartsWith($resolvedTest + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath (Join-Path $resolvedOld '.acceptance-owned'))) { throw 'Unowned old fixture.' }
    Remove-Item -LiteralPath $resolvedOld -Recurse -Force
}
$previousPackage = Join-Path $taskRoot "dist\OpenLnku-$OldVersion-windows-x64"
if (-not (Test-Path -LiteralPath $previousPackage)) { $previousPackage = Join-Path $taskRoot "dist\OpenNex-$OldVersion-windows-x64" }
Copy-Item -LiteralPath $previousPackage -Destination $oldPackage -Recurse
if (Test-Path -LiteralPath (Join-Path $oldPackage 'OpenNex.exe')) {
    Rename-Item -LiteralPath (Join-Path $oldPackage 'OpenNex.exe') -NewName 'OpenLnku.exe'
}
[IO.File]::WriteAllText((Join-Path $oldPackage '.acceptance-owned'), 'OpenLnku isolated installer acceptance')
[IO.File]::WriteAllText((Join-Path $oldPackage 'obsolete-managed.txt'), 'Old managed file: must disappear during upgrade.')
& (Join-Path $PSScriptRoot 'installer.ps1') -Acceptance -FileOnlyAcceptance:$FileOnly -Version $OldVersion -PackageDirectory $oldPackage -OutputFile (Join-Path $testRoot 'setup-old.exe')
& (Join-Path $PSScriptRoot 'installer.ps1') -Acceptance -FileOnlyAcceptance:$FileOnly -OutputFile (Join-Path $testRoot 'setup-new.exe')
