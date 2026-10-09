#requires -Version 7.4
param([string]$QtRoot = (Join-Path $PSScriptRoot '..\.deps\Qt\6.8.3\msvc2022_64'))
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtSdk = (Resolve-Path $QtRoot).Path
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
$publisherIntegrity = & (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')
if ($publisherIntegrity.integrityRID -lt 8192) { throw 'Packaging requires normal Medium user integrity. Low tool execution cannot repair release labels.' }
$outputDir = Join-Path $taskRoot ("dist\OpenNex-$version-windows-x64")
$executable = Join-Path $taskRoot 'build\OpenNex.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build the Qt application first.' }
if (Test-Path -LiteralPath $outputDir) {
    $resolvedOutput = (Resolve-Path $outputDir).Path
    $expectedParent = (Join-Path $taskRoot 'dist') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedOutput.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath (Join-Path $resolvedOutput '.jlu-generated'))) { throw 'Refusing to replace a package directory without its generated marker.' }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $outputDir '.jlu-generated'), 'OpenNex package staging directory')
Copy-Item -LiteralPath $executable -Destination $outputDir
$deployTemp = Join-Path $taskRoot '.deps\deploy-temp'
New-Item -ItemType Directory -Path $deployTemp -Force | Out-Null
$deployArguments = @('--release', '--qmldir', ('"' + (Join-Path $taskRoot 'ui') + '"'), '--include-plugins', 'qoffscreen', '--no-translations', '--no-compiler-runtime', ('"' + (Join-Path $outputDir 'OpenNex.exe') + '"'))
$deploy = Start-Process -FilePath (Join-Path $qtSdk 'bin\windeployqt.exe') -ArgumentList $deployArguments -WindowStyle Hidden -Wait -PassThru -Environment @{ TEMP = $deployTemp; TMP = $deployTemp; PATH = (Join-Path $qtSdk 'bin') + ';' + $env:PATH } -RedirectStandardOutput (Join-Path $taskRoot 'docs\qtdeploy.log') -RedirectStandardError (Join-Path $taskRoot 'docs\qtdeploy-error.log')
if ($deploy.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $taskRoot 'docs\qtdeploy-error.log')
    throw 'windeployqt failed; see docs/qtdeploy-error.log.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$redistRoot = Join-Path $vsRoot 'VC\Redist\MSVC'
$redist = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object Name -Match '^\d+\.' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$crt = Get-ChildItem -LiteralPath (Join-Path $redist.FullName 'x64') -Directory | Where-Object Name -Like '*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'MSVC x64 app-local CRT was not found.' }
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $outputDir }
foreach ($name in @('README.md', 'LICENSE', 'THIRD_PARTY.md')) { Copy-Item -LiteralPath (Join-Path $taskRoot $name) -Destination $outputDir }
$licenses = Join-Path $taskRoot 'packaging\qt-licenses'
if (-not (Test-Path -LiteralPath (Join-Path $licenses 'manifest.json'))) { throw 'Qt license materials are missing.' }
$licenseDir = Join-Path $outputDir 'Qt-Licenses'
New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
Get-ChildItem -LiteralPath $licenses | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $licenseDir -Recurse -Force }
$sourceDir = Join-Path $outputDir 'Source'
New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
foreach ($name in @('src', 'ui', 'tests', 'assets')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $name) -Destination $sourceDir -Recurse -Force
}
$sourcePackaging = Join-Path $sourceDir 'packaging'
New-Item -ItemType Directory -Path $sourcePackaging -Force | Out-Null
foreach ($name in @('installer.nsi', 'windows.rc.in', 'NSIS-COPYING.txt', 'update-public-key.hex', 'qt-licenses')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('packaging\' + $name)) -Destination $sourcePackaging -Recurse -Force
}
$sourceTools = Join-Path $sourceDir 'tools'
New-Item -ItemType Directory -Path $sourceTools -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'tools') -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $sourceTools }
$sourceDocs = Join-Path $sourceDir 'docs'
New-Item -ItemType Directory -Path $sourceDocs -Force | Out-Null
foreach ($name in @("release-$version.md")) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('docs\' + $name)) -Destination $sourceDocs
}
foreach ($name in @('CMakeLists.txt', 'VERSION.txt', 'README.md', 'LICENSE', 'THIRD_PARTY.md', '.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $name) -Destination $sourceDir -Force
}
# The application sources and dynamic libraries remain accessible for rebuilding/replacing.
& (Join-Path $PSScriptRoot 'repair-release-integrity.ps1') -ReleaseOnly |
    Set-Content -LiteralPath (Join-Path $taskRoot "docs\integrity-repair-$version.json") -Encoding utf8
$payload = @(Get-ChildItem -LiteralPath $outputDir -Recurse -Force -File | Sort-Object FullName | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($outputDir,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
[IO.File]::WriteAllText((Join-Path $outputDir 'payload-manifest.json'), (@{version=$version;files=$payload}|ConvertTo-Json -Depth 5 -Compress),[Text.UTF8Encoding]::new($false))
$zipPath = Join-Path $taskRoot ("dist\OpenNex-$version-windows-x64.zip")
Compress-Archive -LiteralPath $outputDir -DestinationPath $zipPath -Force
Write-Output $zipPath
& (Join-Path $PSScriptRoot 'check-release-integrity.ps1') -Executable (Join-Path $outputDir 'OpenNex.exe') |
    Set-Content -LiteralPath (Join-Path $taskRoot "docs\release-integrity-$version.json") -Encoding utf8
Write-Output 'Release executable integrity: verified normal. Distribute the ZIP or use the verified staging executable.'
