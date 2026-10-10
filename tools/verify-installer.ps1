#requires -Version 7.4
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
$testRoot = Join-Path $taskRoot '.deps\release-acceptance'
$installDir = Join-Path $testRoot '安装 空间'
$shortcutDir = Join-Path $testRoot 'shortcuts'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLnkuAcceptance'
$settingsKey = 'HKCU:\Software\OpenLnkuAcceptance\OpenLnkuAcceptance'
$legacyKey = 'HKCU:\Software\OpenLnkuAcceptanceLegacy\OpenLnkuAcceptanceLegacy'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
# A normal terminal cannot compensate for a Low-labeled executable. Check before touching test registry keys.
foreach ($candidate in @((Join-Path $testRoot 'setup-old.exe'), (Join-Path $testRoot 'setup-new.exe'), (Join-Path $taskRoot 'build\settings_fixture.exe'))) {
    & (Join-Path $PSScriptRoot 'check-release-integrity.ps1') -Executable $candidate -RequireNormal | Out-Null
}
$results = [System.Collections.Generic.List[object]]::new()
$installerEnvironment = @{ TEMP = (Join-Path $testRoot 'nsis-temp'); TMP = (Join-Path $testRoot 'nsis-temp') }
$nativeEnvironment = @{
    PATH = (Join-Path $env:WINDIR 'System32') + ';' + $env:WINDIR
    QT_QPA_PLATFORM = 'windows'
    QT_QUICK_BACKEND = 'software'
    QT_FORCE_STDERR_LOGGING = '1'
    QT_PLUGIN_PATH = ''
    QML2_IMPORT_PATH = ''
}
function Check([bool]$ok, [string]$name) {
    $results.Add([pscustomobject]@{ check = $name; passed = $ok })
    Write-Output "$name : $ok"
    if (-not $ok) { throw "Acceptance failed: $name" }
}
function ReadSharedLog([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
        try { $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function SnapshotProduction {
    $paths = @('HKCU:\Software\OpenLnku\OpenLnku', 'HKCU:\Software\OpenNex\OpenNex', 'HKCU:\Software\JluConnect\JluConnect',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLnku', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNex')
    $snapshot = [ordered]@{}
    foreach ($path in $paths) {
        $snapshot[$path] = if (Test-Path -LiteralPath $path) {
            (Get-ItemProperty -LiteralPath $path).PSObject.Properties | Where-Object Name -NotLike 'PS*' | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }
        } else { 'absent' }
    }
    foreach ($name in @('OpenLnku','OpenNex','JluConnect')) { $snapshot["Run-$name"] = ReadRun $name }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($snapshot | ConvertTo-Json -Compress -Depth 5))
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}
function Install([string]$name, [int]$expected = 0) {
    $process = Start-Process -FilePath (Join-Path $testRoot $name) -ArgumentList @('/S', "/D=$installDir") -Environment $installerEnvironment -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $testRoot 'install-stdout.log') -RedirectStandardError (Join-Path $testRoot 'install-stderr.log')
    Check ($process.ExitCode -eq $expected) "$name exit=$($process.ExitCode) expected=$expected"
}
function Uninstall([switch]$Purge, [int]$Expected = 0) {
    # A reused destination can retain an older Low label despite a Medium source.
    $copy = Join-Path $testRoot ('uninstall-run-' + [Guid]::NewGuid().ToString('N') + '.exe')
    Copy-Item -LiteralPath (Join-Path $installDir 'Uninstall.exe') -Destination $copy -Force
    $args = @('/S')
    if ($Purge) { $args += '/PURGESETTINGS' }
    # _?= is deliberately last; NSIS treats the remainder as a Unicode path including spaces.
    $args += "_?=$installDir"
    try {
        & (Join-Path $PSScriptRoot 'check-release-integrity.ps1') -Executable $copy -RequireNormal | Out-Null
        $process = Start-Process -FilePath $copy -ArgumentList $args -Environment $installerEnvironment -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $testRoot 'uninstall-stdout.log') -RedirectStandardError (Join-Path $testRoot 'uninstall-stderr.log')
    } finally { Remove-Item -LiteralPath $copy -Force }
    Check ($process.ExitCode -eq $Expected) "uninstall exit=$($process.ExitCode) expected=$Expected purge=$Purge"
}
function ReadRun([string]$name = 'OpenLnkuAcceptance') {
    $key = Get-Item -LiteralPath $runKey -ErrorAction SilentlyContinue
    if ($key) { $key.GetValue($name, $null) }
}
function Disabled([string]$name) {
    $value = Get-ItemPropertyValue -LiteralPath $settingsKey -Name $name
    return "$value" -eq '0' -or "$value" -eq 'false'
}
function Fixture([string]$mode) {
    $process = Start-Process -FilePath (Join-Path $taskRoot 'build\settings_fixture.exe') -ArgumentList $mode -Environment @{PATH = (Join-Path $taskRoot '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH} -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $testRoot 'fixture-stdout.log') -RedirectStandardError (Join-Path $testRoot 'fixture-stderr.log')
    Check ($process.ExitCode -eq 0) "native Qt settings fixture=$mode"
}
foreach ($path in @($uninstallKey, $settingsKey, $legacyKey)) { if (Test-Path -LiteralPath $path) { throw "Test scope already exists: $path" } }
if ((Get-Item -LiteralPath $runKey).GetValueNames() | Where-Object { $_ -in @('OpenLnkuAcceptance','OpenLnkuAcceptanceLegacy') }) { throw 'Test Run scope already exists.' }
if (Test-Path -LiteralPath $installDir) { throw 'Test install directory already exists; preserve evidence before rerunning.' }
if (Test-Path -LiteralPath $shortcutDir) { throw 'Test shortcut directory already exists; preserve it before rerunning.' }
$productionBefore = SnapshotProduction
$lock = $null
try {
    Install 'setup-old.exe'
    Check ((Get-ItemPropertyValue -LiteralPath $uninstallKey -Name DisplayVersion) -eq '0.1.1') 'first install version'
    Check ((Get-ItemPropertyValue -LiteralPath $settingsKey -Name autoLogin) -eq 1 -and (Get-ItemPropertyValue -LiteralPath $settingsKey -Name autoReconnect) -eq 1) 'first install defaults on'
    Check ((ReadRun).Contains('--demo') -and (ReadRun).Contains('--tray')) 'isolated autorun defaults on and offline'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $shortcutDir 'OpenLnku.lnk'))
    Check ($shortcut.TargetPath -eq (Join-Path $installDir 'OpenLnku.exe')) 'shortcut target'
    $userFile = Join-Path $installDir 'user-note.txt'
    [IO.File]::WriteAllText($userFile, 'User-owned file: retain on upgrade and uninstall.')
    Fixture 'current'
    $nativeKind = (Get-Item -LiteralPath $settingsKey).GetValueKind('autoLogin').ToString()
    Check ($nativeKind -in @('String','DWord')) "native Qt bool registry kind=$nativeKind"
    Remove-ItemProperty -LiteralPath $runKey -Name OpenLnkuAcceptance
    Install 'setup-new.exe'
    Check ((Get-ItemPropertyValue -LiteralPath $uninstallKey -Name DisplayVersion) -eq $version) 'upgrade version'
    Check ((Get-ItemPropertyValue -LiteralPath $uninstallKey -Name InstallLocation) -eq $installDir) 'upgrade preserves Unicode installation path'
    Check ((Get-Item -LiteralPath (Join-Path $installDir 'OpenLnku.exe')).VersionInfo.ProductVersion -eq $version) 'upgraded executable version'
    $missingManaged = @([IO.File]::ReadAllLines((Join-Path $installDir '.opennex-files.txt')) | Where-Object { $_ -and -not $_.EndsWith('\') -and -not (Test-Path -LiteralPath (Join-Path $installDir $_)) })
    Check ($missingManaged.Count -eq 0) 'all upgraded managed files present'
    Check ((Disabled 'autoLogin') -and (Disabled 'autoReconnect') -and (Disabled 'startWithWindows') -and -not (ReadRun)) 'upgrade preserves disabled Qt preferences'
    Check (-not (Test-Path -LiteralPath (Join-Path $installDir 'obsolete-managed.txt'))) 'obsolete managed file removed'
    Check (Test-Path -LiteralPath $userFile) 'upgrade retains unknown user file'
    Check ((Get-ItemPropertyValue -LiteralPath $settingsKey -Name username) -eq 'fixture-account') 'upgrade retains synthetic account'
    Install 'setup-new.exe'
    Check ((Get-ChildItem -LiteralPath $shortcutDir -Filter '*.lnk').Count -eq 2) 'repair does not duplicate shortcuts'
    $uiIni = Join-Path $testRoot 'native-demo.ini'
    $nativeLog = Join-Path $testRoot 'native-tray.log'
    $process = Start-Process -FilePath (Join-Path $installDir 'OpenLnku.exe') -ArgumentList @('--demo', '--tray', '--verify-tray', '--test-identity', 'OpenLnkuAcceptance', '--settings-file', ('"' + $uiIni + '"')) -WorkingDirectory $installDir -Environment $nativeEnvironment -WindowStyle Hidden -Wait -PassThru -RedirectStandardError $nativeLog
    Check ($process.ExitCode -eq 0) 'native Windows tray / window / menu acceptance'
    Check ([IO.File]::ReadAllText($nativeLog).Contains('hide=1 connect=1 disconnect=1 restore=1 demo=1')) 'native tray actions verified'
    $busyLog = Join-Path $testRoot 'busy-demo.log'
    $busy = Start-Process -FilePath (Join-Path $installDir 'OpenLnku.exe') -ArgumentList @('--demo', '--tray', '--quit-after', '15000', '--test-identity', 'OpenLnkuAcceptance', '--settings-file', ('"' + $uiIni + '"')) -WorkingDirectory $installDir -Environment $nativeEnvironment -WindowStyle Hidden -PassThru -RedirectStandardError $busyLog
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        if (Test-Path $busyLog) { $ready = (ReadSharedLog $busyLog).Contains('offline mode:') }
        if ($ready) { break }
        Start-Sleep -Milliseconds 100
    }
    Check $ready 'isolated client running'
    Install 'setup-new.exe' 17
    Uninstall -Expected 17
    $busy.WaitForExit(20000) | Out-Null
    Check ($busy.HasExited -and $busy.ExitCode -eq 0) 'isolated client graceful timed exit'
    $mutex = [Threading.Mutex]::new($false, 'Local\OpenLnkuAcceptance.Installer')
    try {
        Install 'setup-new.exe' 18
        Uninstall -Expected 18
        $blocked = Start-Process -FilePath (Join-Path $installDir 'OpenLnku.exe') -ArgumentList @('--demo','--test-identity','OpenLnkuAcceptance','--settings-file',('"' + $uiIni + '"')) -Environment $nativeEnvironment -WindowStyle Hidden -Wait -PassThru -RedirectStandardError (Join-Path $testRoot 'installer-lock-demo.log')
        Check ($blocked.ExitCode -eq 4) 'client blocked during installer lock'
    } finally { $mutex.Dispose() }
    $lock = [IO.File]::Open((Join-Path $installDir 'README.md'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    Uninstall -Expected 20
    Check ((Test-Path -LiteralPath $uninstallKey) -and (Test-Path -LiteralPath (Join-Path $installDir '.opennex-installed'))) 'failed uninstall retains retry metadata'
    $lock.Dispose(); $lock = $null
    Uninstall
    Check (-not (Test-Path -LiteralPath $uninstallKey) -and -not (Test-Path -LiteralPath (Join-Path $installDir 'OpenLnku.exe')) -and -not (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall.exe'))) 'retry uninstall removes registration and managed executables'
    Check ((Test-Path -LiteralPath $userFile) -and (Test-Path -LiteralPath $settingsKey)) 'default uninstall preserves user file and settings'
    # Remaining unknown user files intentionally prevent this directory from being treated as empty.
    Remove-Item -LiteralPath $userFile
    if (Test-Path -LiteralPath $installDir) { Remove-Item -LiteralPath $installDir }
    Install 'setup-new.exe'
    Check ((Disabled 'autoLogin') -and (Disabled 'autoReconnect')) 'reinstall preserves saved disabled preferences'
    Uninstall -Purge
    Check (-not (Test-Path -LiteralPath $settingsKey)) 'purge removes settings'
    Fixture 'legacy'
    # Synthetic legacy account preferences are off, while startup is explicitly on.
    Set-ItemProperty -LiteralPath $legacyKey -Name startWithWindows -Value 'true'
    New-ItemProperty -LiteralPath $runKey -Name OpenLnkuAcceptanceLegacy -Value ('"' + (Join-Path $testRoot 'legacy.exe') + '" --tray') -PropertyType String -Force | Out-Null
    Install 'setup-new.exe'
    Check ((Disabled 'autoLogin') -and (Disabled 'autoReconnect')) 'native legacy Qt booleans retained'
    Check (-not (ReadRun 'OpenLnkuAcceptanceLegacy') -and (ReadRun).Contains('--demo')) 'legacy autorun migrated to isolated offline app'
    Uninstall -Purge
    Check (-not (Test-Path -LiteralPath $legacyKey)) 'purge removes legacy synthetic settings'
    New-Item -Path $legacyKey -Force | Out-Null
    New-ItemProperty -LiteralPath $legacyKey -Name autoLogin -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $legacyKey -Name autoReconnect -Value 0 -PropertyType DWord -Force | Out-Null
    Install 'setup-new.exe'
    Check ((Disabled 'autoLogin') -and (Disabled 'autoReconnect')) 'legacy DWORD false retained'
    Uninstall -Purge
    Install 'setup-new.exe'
    Check ((Get-ItemPropertyValue -LiteralPath $settingsKey -Name autoLogin) -eq 1 -and (Get-ItemPropertyValue -LiteralPath $settingsKey -Name autoReconnect) -eq 1 -and (ReadRun).Contains('--tray')) 'clean release install defaults all three on'
    $foreign = '"' + (Join-Path $testRoot 'foreign.exe') + '" --tray'
    Set-ItemProperty -LiteralPath $runKey -Name OpenLnkuAcceptance -Value $foreign
    Uninstall -Purge
    Check ((ReadRun) -eq $foreign) 'uninstall preserves autorun belonging to different path'
    Check (-not (Test-Path -LiteralPath $shortcutDir)) 'uninstall removes shortcuts'
    Check ((SnapshotProduction) -eq $productionBefore) 'production settings / startup / install registration unchanged'
} finally {
    if ($lock) { $lock.Dispose() }
    # All these identities were confirmed absent before this test; no production keys are touched.
    foreach ($name in @('OpenLnkuAcceptance','OpenLnkuAcceptanceLegacy')) {
        if ($null -ne (ReadRun $name)) { Remove-ItemProperty -LiteralPath $runKey -Name $name }
    }
    foreach ($path in @($uninstallKey,$settingsKey,$legacyKey)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    }
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskRoot 'docs\installer-acceptance.json') -Encoding utf8
}
Write-Output 'Isolated NSIS installation, upgrade, repair, native tray and uninstall: passed.'
