#requires -Version 7.4
param(
    [int]$ProcessTimeoutMilliseconds = 180000
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$depsRoot = [IO.Path]::GetFullPath((Join-Path $taskRoot '.deps'))
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }

# These identities are deliberately synthetic and are never used by the product.
$appId = 'OpenLnkuAcceptanceBrand'
$legacyId = 'OpenLnkuAcceptanceBrandLegacy'
$runId = [Guid]::NewGuid().ToString('N')
$testRoot = [IO.Path]::GetFullPath((Join-Path $depsRoot ('brand-migration-' + $runId)))
$installDir = [IO.Path]::GetFullPath((Join-Path $testRoot '安装 空间'))
$shortcutDir = [IO.Path]::GetFullPath((Join-Path $testRoot 'shortcuts'))
$legacyShortcutDir = [IO.Path]::GetFullPath((Join-Path $testRoot 'legacy-shortcuts'))
$nsisTemp = [IO.Path]::GetFullPath((Join-Path $testRoot 'nsis-temp'))
$packageDir = [IO.Path]::GetFullPath((Join-Path $taskRoot "dist\OpenLnku-$version-windows-x64"))
$compiler = [IO.Path]::GetFullPath((Join-Path $taskRoot '.deps\NSIS\nsis-3.13\Bin\makensis.exe'))
$migrationTest = [IO.Path]::GetFullPath((Join-Path $taskRoot 'build\localization_tests.exe'))
$settingsKey = "HKCU:\Software\$appId\$appId"
$legacySettingsKey = "HKCU:\Software\$legacyId\$legacyId"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$legacyUninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$legacyId"
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$manifestPath = Join-Path $testRoot 'package-manifest.txt'
$setupPath = Join-Path $testRoot 'setup-brand.exe'
$reportPath = Join-Path $testRoot 'brand-migration-report.json'
$logDir = Join-Path $testRoot 'logs'
$checks = [System.Collections.Generic.List[object]]::new()
$startedUtc = [DateTime]::UtcNow
$overall = 'NotRun'
$failure = $null
$userNote = Join-Path $installDir 'user-note.txt'
$userNoteContent = 'Synthetic user-owned file; do not remove during upgrade.'

function Test-ChildPath([string]$Path, [string]$Parent) {
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\')
    return $fullPath.StartsWith($fullParent + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Add-Check([bool]$Passed, [string]$Name, [string]$Details = '') {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Passed; details = $Details })
    Write-Output ("{0}: {1}{2}" -f $Name, $Passed, $(if ($Details) { " — $Details" } else { '' }))
    if (-not $Passed) { throw "Brand migration acceptance failed: $Name. $Details" }
}

function Start-IsolatedProcess([string]$FilePath, [string[]]$ArgumentList, [string]$LogStem, [string]$WorkingDirectory = $testRoot) {
    $stdoutPath = Join-Path $logDir ($LogStem + '.stdout.log')
    $stderrPath = Join-Path $logDir ($LogStem + '.stderr.log')
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList `
        -WorkingDirectory $WorkingDirectory `
        -Environment @{ TEMP = $nsisTemp; TMP = $nsisTemp } `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $process.WaitForExit($ProcessTimeoutMilliseconds)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Timed out and stopped isolated test process '$FilePath'. Logs: $stdoutPath; $stderrPath"
    }
    return $process
}

function Read-RegistryValue([string]$KeyPath, [string]$Name) {
    if (-not (Test-Path -LiteralPath $KeyPath)) { return $null }
    return (Get-Item -LiteralPath $KeyPath).GetValue($Name, $null)
}

function Test-FalseValue([object]$Value) {
    return $null -ne $Value -and "$Value" -in @('0', 'False', 'false')
}

function Assert-NoReparseAncestors([string]$Path, [string]$StopAt) {
    $current = [IO.Path]::GetFullPath($Path)
    $stop = [IO.Path]::GetFullPath($StopAt)
    while ($current -and $current.StartsWith($stop, [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing a reparse point in test paths: $current"
            }
        }
        if ($current -eq $stop) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

try {
    Add-Check (Test-ChildPath -Path $testRoot -Parent $depsRoot) 'test root is below workspace .deps' $testRoot
    Add-Check (Test-ChildPath -Path $installDir -Parent $depsRoot) 'installation target is below workspace .deps' $installDir
    Add-Check (-not (Test-Path -LiteralPath $testRoot)) 'unique evidence directory is absent' $testRoot
    Assert-NoReparseAncestors -Path $depsRoot -StopAt $taskRoot
    Add-Check ((Test-Path -LiteralPath $packageDir -PathType Container) -and (Test-ChildPath -Path $packageDir -Parent $taskRoot)) 'release package exists inside workspace' $packageDir
    Add-Check (Test-Path -LiteralPath $compiler -PathType Leaf) 'local NSIS compiler exists' $compiler
    Add-Check (Test-Path -LiteralPath $migrationTest -PathType Leaf) 'offline QSettings migration test exists' $migrationTest
    Assert-NoReparseAncestors -Path $packageDir -StopAt $taskRoot
    Assert-NoReparseAncestors -Path $compiler -StopAt $taskRoot
    Assert-NoReparseAncestors -Path $migrationTest -StopAt $taskRoot

    # Do all artifact/integrity preflight before creating any synthetic registry values.
    $integrityCheck = Join-Path $PSScriptRoot 'check-release-integrity.ps1'
    & $integrityCheck -Executable $compiler -RequireNormal | Out-Null
    & $integrityCheck -Executable $migrationTest -RequireNormal | Out-Null
    $publisherIntegrity = & (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')
    Add-Check ($publisherIntegrity.integrityRID -ge 8192) 'PowerShell runs at Medium integrity' "RID=$($publisherIntegrity.integrityRID)"

    foreach ($key in @($settingsKey, $legacySettingsKey, $uninstallKey, $legacyUninstallKey)) {
        Add-Check (-not (Test-Path -LiteralPath $key)) 'synthetic registry key is absent before test' $key
    }
    $runValues = (Get-Item -LiteralPath $runKey).GetValueNames()
    Add-Check (-not ($runValues -contains $appId) -and -not ($runValues -contains $legacyId)) 'synthetic Run values are absent before test' "$appId / $legacyId"

    New-Item -ItemType Directory -Path $testRoot | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot '.brand-migration-owned'), $runId, [Text.UTF8Encoding]::new($false))
    & (Join-Path $PSScriptRoot 'repair-release-integrity.ps1') -BrandAcceptanceRoot $testRoot | Out-Null
    New-Item -ItemType Directory -Path $logDir | Out-Null
    New-Item -ItemType Directory -Path $nsisTemp | Out-Null
    Add-Check (((Get-Item -LiteralPath $testRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'evidence root is not a reparse point' ''

    # Build an isolated installer directly so its manifest, output, logs, and TEMP all stay under this GUID root.
    $packageItems = Get-ChildItem -LiteralPath $packageDir -Recurse -Force
    $manifestEntries = [System.Collections.Generic.List[string]]::new()
    foreach ($item in $packageItems) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package contains a reparse point: $($item.FullName)" }
        $relative = [IO.Path]::GetRelativePath($packageDir, $item.FullName)
        if ($relative.Contains('..') -or $relative.Contains(':') -or [IO.Path]::IsPathRooted($relative) -or $relative.Length -gt 900) {
            throw "Invalid package manifest path: $relative"
        }
        $manifestEntries.Add($(if ($item.PSIsContainer) { $relative + '\' } else { $relative }))
    }
    $manifestArray = $manifestEntries.ToArray()
    [Array]::Sort($manifestArray, [StringComparer]::OrdinalIgnoreCase)
    [IO.File]::WriteAllLines($manifestPath, $manifestArray, [Text.UTF8Encoding]::new($false))
    Add-Check ($manifestArray -contains 'OpenLnku.exe') 'release manifest includes current executable' ''

    $compilerArgs = @(
        '/V2', '/INPUTCHARSET', 'UTF8', "/DAPP_VERSION=$version", "/DAPP_ID=$appId", "/DAPP_TITLE=$appId",
        ('/DPACKAGE_DIR="' + $packageDir + '"'), ('/DMANIFEST_FILE="' + $manifestPath + '"'), ('/DOUTPUT_FILE="' + $setupPath + '"'),
        '/DISOLATED_ACCEPTANCE', "/DLEGACY_ID=$legacyId", ('"' + (Join-Path $taskRoot 'packaging\installer.nsi') + '"')
    )
    $compile = Start-IsolatedProcess -FilePath $compiler -ArgumentList $compilerArgs -LogStem 'compile-installer' -WorkingDirectory (Join-Path $taskRoot 'packaging')
    Add-Check ($compile.ExitCode -eq 0 -and (Test-Path -LiteralPath $setupPath -PathType Leaf)) 'isolated NSIS installer compiles' "exit=$($compile.ExitCode)"
    & $integrityCheck -Executable $setupPath -RequireNormal | Out-Null

    # This invokes only an offline QtTest case using temporary INI files; no controller or client is launched.
    $qtBin = Join-Path $taskRoot '.deps\Qt\6.8.3\msvc2022_64\bin'
    $testEnv = @{
        PATH = $qtBin + ';' + (Join-Path $env:WINDIR 'System32') + ';' + $env:WINDIR
        QT_QPA_PLATFORM = 'offscreen'
        TEMP = $nsisTemp
        TMP = $nsisTemp
    }
    $migration = Start-Process -FilePath $migrationTest -ArgumentList @('rebrandingPreservesCredentialAndExplicitChoices') `
        -WorkingDirectory (Split-Path -Parent $migrationTest) -Environment $testEnv `
        -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $logDir 'qsettings-migration.stdout.log') `
        -RedirectStandardError (Join-Path $logDir 'qsettings-migration.stderr.log')
    Add-Check ($migration.ExitCode -eq 0) 'offline QSettings migration preserves synthetic credential, language, and explicit preferences' "exit=$($migration.ExitCode)"

    # Seed only fake legacy state. The old executable is inert text and is never started.
    New-Item -ItemType Directory -Path $installDir | Out-Null
    [IO.File]::WriteAllText((Join-Path $installDir '.opennex-installed'), $legacyId, [Text.UTF8Encoding]::new($false))
    [string[]]$oldManifest = @('OpenNex.exe', 'obsolete-managed.txt')
    [Array]::Sort($oldManifest, [StringComparer]::OrdinalIgnoreCase)
    [IO.File]::WriteAllLines((Join-Path $installDir '.opennex-files.txt'), $oldManifest, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $installDir 'OpenNex.exe'), 'synthetic legacy executable; never run', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $installDir 'obsolete-managed.txt'), 'synthetic managed legacy file', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($userNote, $userNoteContent, [Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Path $legacyShortcutDir | Out-Null
    [IO.File]::WriteAllText((Join-Path $legacyShortcutDir ($legacyId + '.lnk')), 'synthetic shortcut', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $legacyShortcutDir 'Uninstall.lnk'), 'synthetic legacy uninstall shortcut', [Text.UTF8Encoding]::new($false))

    New-Item -Path $legacyUninstallKey -Force | Out-Null
    New-ItemProperty -LiteralPath $legacyUninstallKey -Name InstallLocation -Value $installDir -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath $legacyUninstallKey -Name DisplayVersion -Value '1.0.0' -PropertyType String -Force | Out-Null
    New-Item -Path $legacySettingsKey -Force | Out-Null
    New-ItemProperty -LiteralPath $legacySettingsKey -Name username -Value 'synthetic-account' -PropertyType String -Force | Out-Null
    # Opaque placeholder only; never a real credential or a decryptable DPAPI blob.
    New-ItemProperty -LiteralPath $legacySettingsKey -Name credential -Value 'c3ludGhldGljLW9ubHktZW5jcnlwdGVkLWJ5dGVz' -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath $legacySettingsKey -Name language -Value 'en' -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath $legacySettingsKey -Name autoLogin -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $legacySettingsKey -Name autoReconnect -Value 0 -PropertyType DWord -Force | Out-Null
    Add-Check (-not ((Get-Item -LiteralPath $legacySettingsKey).GetValueNames() -contains 'startWithWindows')) 'legacy startup boolean is intentionally missing' ''
    $legacyRun = '"' + (Join-Path $installDir 'OpenNex.exe') + '" --tray'
    New-ItemProperty -LiteralPath $runKey -Name $legacyId -Value $legacyRun -PropertyType String -Force | Out-Null

    $setup = Start-IsolatedProcess -FilePath $setupPath -ArgumentList @('/S', "/D=$installDir") -LogStem 'install-brand'
    Add-Check ($setup.ExitCode -eq 0) 'brand migration installer exits successfully' "exit=$($setup.ExitCode)"
    Add-Check (([IO.File]::ReadAllText((Join-Path $installDir '.opennex-installed'))).Trim() -eq $appId) 'installation ownership marker migrates to current identity' ''
    Add-Check ((Get-Item -LiteralPath (Join-Path $installDir 'OpenLnku.exe')).VersionInfo.ProductVersion -eq $version) 'current OpenLnku executable installed' "version=$version"
    Add-Check (-not (Test-Path -LiteralPath (Join-Path $installDir 'OpenNex.exe')) -and -not (Test-Path -LiteralPath (Join-Path $installDir 'obsolete-managed.txt'))) 'old managed executable and obsolete file are removed' ''
    Add-Check (([IO.File]::ReadAllText($userNote)) -eq $userNoteContent) 'unmanaged user file survives brand upgrade' ''
    Add-Check ((Test-Path -LiteralPath (Join-Path $shortcutDir 'OpenLnku.lnk')) -and (Test-Path -LiteralPath (Join-Path $shortcutDir 'Uninstall.lnk'))) 'current-brand shortcuts are created' ''
    Add-Check (-not (Test-Path -LiteralPath $legacyShortcutDir)) 'legacy-brand shortcuts are removed' ''
    Add-Check (-not (Test-Path -LiteralPath $legacyUninstallKey) -and (Test-Path -LiteralPath $uninstallKey)) 'legacy uninstall entry is replaced by current entry' ''
    Add-Check ((Read-RegistryValue -KeyPath $uninstallKey -Name InstallLocation) -eq $installDir) 'current uninstall entry keeps the migrated install path' ''
    Add-Check ((Test-FalseValue (Read-RegistryValue -KeyPath $settingsKey -Name autoLogin)) -and (Test-FalseValue (Read-RegistryValue -KeyPath $settingsKey -Name autoReconnect))) 'explicit legacy false preferences are preserved' ''
    Add-Check ((Read-RegistryValue -KeyPath $settingsKey -Name startWithWindows) -eq 1) 'missing legacy startup boolean is inferred from its legacy Run entry' ''
    $currentRun = [string](Read-RegistryValue -KeyPath $runKey -Name $appId)
    Add-Check ($currentRun.Contains(('"' + (Join-Path $installDir 'OpenLnku.exe') + '"')) -and $currentRun.Contains('--demo') -and $currentRun.Contains('--tray') -and -not (Read-RegistryValue -KeyPath $runKey -Name $legacyId)) 'isolated Run entry migrates to current offline command' ''
    Add-Check ((Read-RegistryValue -KeyPath $settingsKey -Name startupConfigured) -eq 1 -and (Read-RegistryValue -KeyPath $settingsKey -Name brandStartupMigrated) -eq 1) 'startup migration markers are set' ''
    Add-Check ((Read-RegistryValue -KeyPath $legacySettingsKey -Name username) -eq 'synthetic-account' -and (Read-RegistryValue -KeyPath $legacySettingsKey -Name language) -eq 'en') 'legacy synthetic profile remains available for first-run settings migration' ''

    # An explicit current-brand opt-out must remain off and must clear a stale old Run
    # command only when it targets the legacy executable in this same install folder.
    Set-ItemProperty -LiteralPath $settingsKey -Name startWithWindows -Value 0
    if (Read-RegistryValue -KeyPath $runKey -Name $appId) {
        Remove-ItemProperty -LiteralPath $runKey -Name $appId
    }
    $samePathLegacyRun = '"' + (Join-Path $installDir 'OpenNex.exe') + '" --tray'
    New-ItemProperty -LiteralPath $runKey -Name $legacyId -Value $samePathLegacyRun -PropertyType String -Force | Out-Null
    $repair = Start-IsolatedProcess -FilePath $setupPath -ArgumentList @('/S', "/D=$installDir") -LogStem 'repair-disabled-same-path-run'
    Add-Check ($repair.ExitCode -eq 0) 'repair with same-path stale legacy Run succeeds' "exit=$($repair.ExitCode)"
    Add-Check (Test-FalseValue (Read-RegistryValue -KeyPath $settingsKey -Name startWithWindows)) 'same-path stale Run does not re-enable startup' ''
    Add-Check ((-not (Read-RegistryValue -KeyPath $runKey -Name $appId)) -and (-not (Read-RegistryValue -KeyPath $runKey -Name $legacyId))) 'same-path stale legacy Run and current Run are both absent' ''

    Set-ItemProperty -LiteralPath $settingsKey -Name startWithWindows -Value 0
    $foreignLegacyRun = '"' + (Join-Path $testRoot 'different-install\OpenNex.exe') + '" --tray'
    New-ItemProperty -LiteralPath $runKey -Name $legacyId -Value $foreignLegacyRun -PropertyType String -Force | Out-Null
    $repairForeign = Start-IsolatedProcess -FilePath $setupPath -ArgumentList @('/S', "/D=$installDir") -LogStem 'repair-disabled-foreign-run'
    Add-Check ($repairForeign.ExitCode -eq 0) 'repair with foreign legacy Run succeeds' "exit=$($repairForeign.ExitCode)"
    Add-Check (Test-FalseValue (Read-RegistryValue -KeyPath $settingsKey -Name startWithWindows)) 'foreign Run does not re-enable current startup' ''
    Add-Check (-not (Read-RegistryValue -KeyPath $runKey -Name $appId)) 'current Run remains absent for disabled preference' ''
    Add-Check ((Read-RegistryValue -KeyPath $runKey -Name $legacyId) -eq $foreignLegacyRun) 'foreign-path legacy Run is preserved' ''
    Remove-ItemProperty -LiteralPath $runKey -Name $legacyId

    # Purge only this synthetic acceptance identity after recording all migration assertions.
    $uninstallerCopy = Join-Path $testRoot ('uninstall-run-' + [Guid]::NewGuid().ToString('N') + '.exe')
    Copy-Item -LiteralPath (Join-Path $installDir 'Uninstall.exe') -Destination $uninstallerCopy
    & $integrityCheck -Executable $uninstallerCopy -RequireNormal | Out-Null
    $uninstall = Start-IsolatedProcess -FilePath $uninstallerCopy -ArgumentList @('/S', '/PURGESETTINGS', "_?=$installDir") -LogStem 'uninstall-brand'
    Add-Check ($uninstall.ExitCode -eq 0) 'isolated acceptance uninstaller exits successfully' "exit=$($uninstall.ExitCode)"
    Remove-Item -LiteralPath $uninstallerCopy -Force
    Add-Check (-not (Test-Path -LiteralPath $settingsKey) -and -not (Test-Path -LiteralPath $legacySettingsKey) -and -not (Test-Path -LiteralPath $uninstallKey)) 'purge removes only the synthetic current and legacy settings' ''
    Add-Check (-not (Read-RegistryValue -KeyPath $runKey -Name $appId) -and -not (Read-RegistryValue -KeyPath $runKey -Name $legacyId)) 'synthetic Run values are absent after uninstall' ''
    Add-Check (-not (Test-Path -LiteralPath $shortcutDir) -and -not (Test-Path -LiteralPath $legacyShortcutDir)) 'synthetic shortcut directories are absent after uninstall' ''
    if (Test-Path -LiteralPath $userNote -PathType Leaf) {
        Add-Check (([IO.File]::ReadAllText($userNote)) -eq $userNoteContent) 'only the marked synthetic user file remains after uninstall' ''
        Remove-Item -LiteralPath $userNote -Force
    }
    if (Test-Path -LiteralPath $installDir -PathType Container) {
        Add-Check ((@(Get-ChildItem -LiteralPath $installDir -Force).Count -eq 0)) 'only an empty isolated install directory remains' ''
        [IO.Directory]::Delete($installDir, $false)
    }
    Add-Check (-not (Test-Path -LiteralPath $installDir)) 'isolated installation directory is removed' ''
    $overall = 'Passed'
} catch {
    $overall = 'Failed'
    $failure = $_.Exception.Message
    throw
} finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $report = [ordered]@{
            schemaVersion = 1
            test = 'isolated-brand-migration'
            appIdentity = $appId
            legacyIdentity = $legacyId
            startedUtc = $startedUtc.ToString('o')
            finishedUtc = [DateTime]::UtcNow.ToString('o')
            overall = $overall
            failure = $failure
            scope = [ordered]@{
                evidenceRoot = $testRoot
                installRoot = $installDir
                syntheticRegistryOnly = $true
                realCredentialsUsed = $false
                applicationStarted = $false
                authenticationOrNetworkUsed = $false
                qsettingsMigrationUnit = 'rebrandingPreservesCredentialAndExplicitChoices'
            }
            checks = @($checks)
            logs = $logDir
        }
        [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    }
}

Write-Output "Isolated brand migration acceptance passed. Report: $reportPath"
