#requires -Version 7.4
param([int]$Page = 0, [switch]$Connected, [string]$Scale = '', [double]$Dpi = 1, [switch]$License)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$env:PATH = (Join-Path $taskRoot '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$env:QML_DISABLE_DISK_CACHE = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QT_SCALE_FACTOR = "$Dpi"
$imageDir = Join-Path $taskRoot 'docs\screenshots'
New-Item -ItemType Directory -Path $imageDir -Force | Out-Null
$suffix = if ($License) { '-license' } elseif ($Scale) { "-$Scale" } elseif ($Dpi -ne 1) { "-dpi$Dpi" } else { '' }
$output = Join-Path $imageDir "page-$Page$suffix.png"
$arguments = @('--demo', '--capture', ('"' + $output + '"'), '--page', "$Page")
if ($Connected) { $arguments += '--demo-connect' }
if ($License) { $arguments += '--demo-license' }
if ($Scale) { $arguments += @('--scale', $Scale) }
$process = Start-Process -FilePath (Join-Path $taskRoot 'build\OpenNex.exe') -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $imageDir "page-$Page$suffix.log")
if ($process.ExitCode -ne 0) { throw "Preview failed: $($process.ExitCode)" }
Write-Output $output
