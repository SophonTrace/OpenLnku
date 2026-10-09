#requires -Version 7.4
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
$packageDir = Join-Path $taskRoot "dist\OpenNex-$version-windows-x64"
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
$process = Start-Process -FilePath (Join-Path $packageDir 'OpenNex.exe') -ArgumentList @('--demo', '--capture', ('"' + $image + '"'), '--demo-connect') -WorkingDirectory $packageDir -Environment $environment -WindowStyle Hidden -Wait -PassThru -RedirectStandardError $stderr
Get-Content -LiteralPath $stderr
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $image)) { throw "Standalone package failed: $($process.ExitCode)" }
$log = [IO.File]::ReadAllText($stderr)
if (-not $log.Contains('demo=1 simulationComplete=1') -or $log.Contains('ReferenceError') -or $log.Contains('TypeError') -or $log.Contains('failed to load')) { throw 'The standalone UI did not complete its synthetic handshake cleanly.' }
Write-Output 'Standalone offline package: passed (SDK paths excluded).'
