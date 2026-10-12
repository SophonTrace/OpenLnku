#requires -Version 7.4
param([ValidatePattern('^[a-z][a-z0-9-]{0,24}$')][string]$PackageSuffix)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
$packageName = "OpenLnku-$version-windows-x64" + $(if ($PackageSuffix) { "-$PackageSuffix" } else { '' })
$packageDir = Join-Path $taskRoot ("dist\" + $packageName)
$imageDir = Join-Path $taskRoot 'docs\screenshots'
New-Item -ItemType Directory -Path $imageDir -Force | Out-Null
$image = Join-Path $imageDir 'portable.png'
$stderr = Join-Path $imageDir 'portable.log'
$environment = @{
    PATH = (Join-Path $env:WINDIR 'System32') + ';' + $env:WINDIR
    QT_QPA_PLATFORM = 'offscreen'
    QT_QUICK_BACKEND = 'software'
    QT_QPA_FONTDIR = (Join-Path $env:WINDIR 'Fonts')
    QML_DISABLE_DISK_CACHE = '1'
    QT_FORCE_STDERR_LOGGING = '1'
    QT_PLUGIN_PATH = ''
    QML2_IMPORT_PATH = ''
}
$process = Start-Process -FilePath (Join-Path $packageDir 'OpenLnku.exe') -ArgumentList @('--demo', '--capture', ('"' + $image + '"'), '--demo-connect') -WorkingDirectory $packageDir -Environment $environment -WindowStyle Hidden -Wait -PassThru -RedirectStandardError $stderr
Get-Content -LiteralPath $stderr
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $image)) { throw "Standalone package failed: $($process.ExitCode)" }
$log = [IO.File]::ReadAllText($stderr)
if (-not $log.Contains('demo=1 simulationComplete=1') -or $log.Contains('ReferenceError') -or $log.Contains('TypeError') -or $log.Contains('failed to load')) { throw 'The standalone UI did not complete its synthetic handshake cleanly.' }
Write-Output 'Standalone offline package: passed (SDK paths excluded).'

# Exercise the new cards with bundled SVG flags and no SDK/plugin fallback.
$networkImage = Join-Path $imageDir 'portable-network-details.png'
$networkError = Join-Path $imageDir 'portable-network-details.log'
$network = Start-Process -FilePath (Join-Path $packageDir 'OpenLnku.exe') -ArgumentList @('--demo', '--capture', ('"' + $networkImage + '"'), '--page', '1', '--network-details', '--demo-connect') -WorkingDirectory $packageDir -Environment $environment -WindowStyle Hidden -Wait -PassThru -RedirectStandardError $networkError
$networkLog = [IO.File]::ReadAllText($networkError)
if ($network.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $networkImage) -or $networkLog -match 'ReferenceError|TypeError|failed to load|Cannot open|Unsupported image format') { throw 'Standalone network cards or flag resources failed.' }
if (-not (Test-Path -LiteralPath (Join-Path $packageDir 'Qt6Svg.dll')) -or -not (Test-Path -LiteralPath (Join-Path $packageDir 'imageformats/qsvg.dll'))) { throw 'Bundled SVG runtime is missing.' }
Write-Output 'Standalone network cards and bundled SVG runtime: passed.'
