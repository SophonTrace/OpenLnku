#requires -Version 7.4
param(
    [string]$NsisRoot = (Join-Path $PSScriptRoot '..\.deps\NSIS\nsis-3.13'),
    [string]$Version,
    [string]$PackageDirectory,
    [string]$OutputFile,
    [switch]$Acceptance,
    [switch]$FileOnlyAcceptance,
    [ValidatePattern('^OpenLnkuAcceptance[A-Za-z0-9_-]*$')][string]$AcceptanceIdentity = 'OpenLnkuAcceptance',
    [ValidatePattern('^OpenLnkuAcceptance[A-Za-z0-9_-]*$')][string]$LegacyAcceptanceIdentity = 'OpenLnkuAcceptanceLegacy'
)
$ErrorActionPreference = 'Stop'
if ($FileOnlyAcceptance -and -not $Acceptance) { throw 'File-only mode requires isolated acceptance.' }
if (-not $Acceptance -and ($PSBoundParameters.ContainsKey('AcceptanceIdentity') -or $PSBoundParameters.ContainsKey('LegacyAcceptanceIdentity'))) { throw 'Test identities require isolated acceptance.' }
if ($Acceptance -and $AcceptanceIdentity -eq $LegacyAcceptanceIdentity) { throw 'Current and legacy test identities must differ.' }
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$publisherIntegrity = & (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')
if ($publisherIntegrity.integrityRID -lt 8192) { throw 'Installer publishing requires normal Medium user integrity.' }
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $taskRoot "dist\OpenLnku-$Version-windows-x64" }
$packageDir = (Resolve-Path -LiteralPath $PackageDirectory).Path
$expectedRoot = $taskRoot + [IO.Path]::DirectorySeparatorChar
if (-not $packageDir.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Package must be within the workspace.' }
if (-not $OutputFile) { $OutputFile = Join-Path $taskRoot "dist\OpenLnku-$Version-setup-x64.exe" }
$outputPath = [IO.Path]::GetFullPath($OutputFile)
if (-not $outputPath.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Installer output must be within the workspace.' }
$compiler = Join-Path (Resolve-Path $NsisRoot).Path 'Bin\makensis.exe'
if ($compiler -eq (Join-Path $taskRoot '.deps\NSIS\nsis-3.13\Bin\makensis.exe')) {
    & (Join-Path $PSScriptRoot 'repair-release-integrity.ps1') -ToolchainOnly | Out-Null
}
$files = Get-ChildItem -LiteralPath $packageDir -Recurse -Force
[string[]]$relativePaths = foreach ($file in $files) {
    $relative = [IO.Path]::GetRelativePath($packageDir, $file.FullName)
    if ($relative.Contains('..') -or $relative.Contains(':') -or [IO.Path]::IsPathRooted($relative) -or $relative.Length -gt 900) { throw 'Invalid package manifest path.' }
    if ($file.PSIsContainer) { $relative + '\' } else { $relative }
}
[Array]::Sort($relativePaths, [StringComparer]::OrdinalIgnoreCase)
$identity = if ($Acceptance) { $AcceptanceIdentity } else { 'OpenLnku' }
$manifest = Join-Path $taskRoot "packaging\manifest-$identity-$Version.txt"
[IO.File]::WriteAllLines($manifest, $relativePaths, [Text.UTF8Encoding]::new($false))
$taskTemp = Join-Path $taskRoot '.deps\nsis-temp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$compilerArgs = @('/V2', '/INPUTCHARSET', 'UTF8', "/DAPP_VERSION=$Version", "/DAPP_ID=$identity", "/DAPP_TITLE=$identity",
    ('/DPACKAGE_DIR="' + $packageDir + '"'), ('/DMANIFEST_FILE="' + $manifest + '"'), ('/DOUTPUT_FILE="' + $outputPath + '"'))
if ($Acceptance) { $compilerArgs += '/DISOLATED_ACCEPTANCE'; $compilerArgs += "/DLEGACY_ID=$LegacyAcceptanceIdentity" }
if ($FileOnlyAcceptance) { $compilerArgs += '/DISOLATED_FILES_ONLY' }
$compilerArgs += '"' + (Join-Path $taskRoot 'packaging\installer.nsi') + '"'
$process = Start-Process -FilePath $compiler -ArgumentList $compilerArgs -WorkingDirectory (Join-Path $taskRoot 'packaging') -Environment @{ TEMP = $taskTemp; TMP = $taskTemp } -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $taskRoot 'docs\nsis.log') -RedirectStandardError (Join-Path $taskRoot 'docs\nsis-error.log')
if ($process.ExitCode -ne 0) { Get-Content -LiteralPath (Join-Path $taskRoot 'docs\nsis-error.log'); throw 'Installer compilation failed.' }
Write-Output $outputPath
& (Join-Path $PSScriptRoot 'repair-release-integrity.ps1') | Out-Null
& (Join-Path $PSScriptRoot 'check-release-integrity.ps1') -Executable $outputPath -RequireNormal | Out-Null
if (-not $Acceptance) {
    # ZIP omits the staging file's NTFS mandatory-integrity label.
    $installerZip = [IO.Path]::ChangeExtension($outputPath, '.zip')
    Compress-Archive -LiteralPath $outputPath -DestinationPath $installerZip -Force
    Write-Output $installerZip
}
