#requires -Version 7.4
param(
    [string]$TestRoot = (Join-Path $PSScriptRoot '..\.deps\release-file-acceptance'),
    [int]$ProcessTimeoutMilliseconds = 180000
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
$oldVersion = (Get-Item -LiteralPath (Join-Path $TestRoot 'package-old\OpenLnku.exe')).VersionInfo.ProductVersion
$testRoot = [IO.Path]::GetFullPath($TestRoot)
$installDir = [IO.Path]::GetFullPath((Join-Path $testRoot '安装 空间'))
$shortcutDir = [IO.Path]::GetFullPath((Join-Path $testRoot 'shortcuts'))
$nsisTemp = [IO.Path]::GetFullPath((Join-Path $taskRoot '.deps\nsis-temp'))
$resultPath = Join-Path $taskRoot 'docs\installer-files-acceptance.json'
$startedUtc = [DateTime]::UtcNow
$runId = [Guid]::NewGuid().ToString('N')
$logDir = Join-Path $testRoot (Join-Path 'logs' $runId)
$results = [System.Collections.Generic.List[object]]::new()
$uninstallerRun = 0
$overall = 'NotRun'
$failure = $null
$readmeLock = $null

function Test-ChildPath([string]$Path, [string]$Parent) {
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\')
    return $fullPath.StartsWith($fullParent + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Add-Check([bool]$Passed, [string]$Name, [string]$Details = '') {
    $results.Add([pscustomobject]@{
        name = $Name
        passed = $Passed
        details = $Details
    })
    Write-Output ("{0}: {1}{2}" -f $Name, $Passed, $(if ($Details) { " — $Details" } else { '' }))
    if (-not $Passed) { throw "File-only installer acceptance failed: $Name. $Details" }
}

function Start-IsolatedProcess([string]$FilePath, [string[]]$ArgumentList, [string]$LogStem) {
    $stdoutPath = Join-Path $logDir ($LogStem + '.stdout.log')
    $stderrPath = Join-Path $logDir ($LogStem + '.stderr.log')
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList `
        -Environment @{ TEMP = $nsisTemp; TMP = $nsisTemp } `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $process.WaitForExit($ProcessTimeoutMilliseconds)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Timed out and stopped isolated process '$FilePath'. Logs: $stdoutPath; $stderrPath"
    }
    return $process
}

function Start-Installer([string]$Name, [int]$ExpectedExitCode = 0) {
    $installerPath = Join-Path $testRoot $Name
    # NSIS requires /D=... to be the final argument and consumes the remaining path, including spaces.
    $process = Start-IsolatedProcess -FilePath $installerPath -ArgumentList @('/S', "/D=$installDir") -LogStem ("install-{0}-{1}" -f $Name, [Guid]::NewGuid().ToString('N'))
    Add-Check ($process.ExitCode -eq $ExpectedExitCode) "$Name exit code" "actual=$($process.ExitCode), expected=$ExpectedExitCode"
}

function Start-Uninstaller([int]$ExpectedExitCode = 0) {
    $uninstallerPath = Join-Path $installDir 'Uninstall.exe'
    if (-not (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
        throw "Managed uninstaller is missing: $uninstallerPath"
    }
    $script:uninstallerRun++
    $runnerCopy = Join-Path $testRoot ("uninstall-run-{0}.exe" -f $script:uninstallerRun)
    if (Test-Path -LiteralPath $runnerCopy) { throw "Refusing to overwrite prior test artifact: $runnerCopy" }
    Copy-Item -LiteralPath $uninstallerPath -Destination $runnerCopy
    try {
        # NSIS requires _?=... last; it may contain spaces and is intentionally not quoted.
        $process = Start-IsolatedProcess -FilePath $runnerCopy -ArgumentList @('/S', "_?=$installDir") -LogStem ("uninstall-{0}" -f $script:uninstallerRun)
        Add-Check ($process.ExitCode -eq $ExpectedExitCode) "uninstall exit code attempt $script:uninstallerRun" "actual=$($process.ExitCode), expected=$ExpectedExitCode"
    } finally {
        if (Test-Path -LiteralPath $runnerCopy -PathType Leaf) { Remove-Item -LiteralPath $runnerCopy -Force }
    }
}

function Read-ManagedManifest([string]$Path) {
    return [IO.File]::ReadAllLines($Path)
}

function Get-MissingManagedFiles([string]$Root, [string[]]$Manifest) {
    $missing = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $Manifest) {
        if ([string]::IsNullOrWhiteSpace($entry) -or $entry.EndsWith('\')) { continue }
        if ($entry.Contains('..') -or $entry.Contains(':') -or $entry.Contains('/') -or [IO.Path]::IsPathRooted($entry) -or $entry.StartsWith('\')) {
            throw "Unsafe path in test installer manifest: $entry"
        }
        $filePath = [IO.Path]::GetFullPath((Join-Path $Root $entry))
        if (-not (Test-ChildPath -Path $filePath -Parent $Root)) { throw "Manifest path escapes test installation: $entry" }
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) { $missing.Add($entry) }
    }
    return $missing.ToArray()
}

try {
    Add-Check (Test-ChildPath -Path $testRoot -Parent $taskRoot) 'test root is inside workspace' $testRoot
    Add-Check (Test-ChildPath -Path $installDir -Parent $testRoot) 'installation directory is inside isolated root' $installDir
    Add-Check (Test-ChildPath -Path $shortcutDir -Parent $testRoot) 'shortcut directory is inside isolated root' $shortcutDir
    Add-Check (Test-ChildPath -Path $nsisTemp -Parent $taskRoot) 'NSIS temp directory is inside workspace' $nsisTemp
    Add-Check (Test-Path -LiteralPath $testRoot -PathType Container) 'acceptance root exists' $testRoot
    $testRootItem = Get-Item -LiteralPath $testRoot -Force
    Add-Check (($testRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'acceptance root is not a reparse point' ''
    foreach ($name in @('setup-old.exe', 'setup-new.exe')) {
        $path = Join-Path $testRoot $name
        Add-Check (Test-Path -LiteralPath $path -PathType Leaf) "$name is present" $path
        $item = Get-Item -LiteralPath $path -Force
        Add-Check (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "$name is a regular file" ''
    }
    Add-Check (-not (Test-Path -LiteralPath $installDir)) 'isolated installation target is absent' $installDir
    Add-Check (-not (Test-Path -LiteralPath $shortcutDir)) 'isolated shortcuts target is absent' $shortcutDir
    $leftoverRunner = Get-ChildItem -LiteralPath $testRoot -Filter 'uninstall-run-*.exe' -File -ErrorAction SilentlyContinue
    Add-Check (-not $leftoverRunner) 'no prior uninstaller runner artifacts' ''

    New-Item -ItemType Directory -Path $nsisTemp -Force | Out-Null
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null

    Start-Installer 'setup-old.exe'
    $markerPath = Join-Path $installDir '.opennex-installed'
    $manifestPath = Join-Path $installDir '.opennex-files.txt'
    $obsoletePath = Join-Path $installDir 'obsolete-managed.txt'
    $readmePath = Join-Path $installDir 'README.md'
    $appPath = Join-Path $installDir 'OpenLnku.exe'

    Add-Check (([IO.File]::ReadAllText($markerPath)).Trim() -eq 'OpenLnkuAcceptance') 'old package ownership marker' ''
    Add-Check ((Get-Item -LiteralPath $appPath).VersionInfo.ProductVersion -eq $oldVersion) 'old package version installed' "ProductVersion=$oldVersion"
    Add-Check (Test-Path -LiteralPath $readmePath -PathType Leaf) 'old package README installed' ''
    Add-Check (Test-Path -LiteralPath $obsoletePath -PathType Leaf) 'old-only managed file installed' ''
    $oldManifest = Read-ManagedManifest $manifestPath
    Add-Check ($oldManifest -contains 'obsolete-managed.txt') 'old manifest owns obsolete file' ''
    $missingOldFiles = @(Get-MissingManagedFiles -Root $installDir -Manifest $oldManifest)
    $missingOldSummary = [string]::Join(', ', @($missingOldFiles | Select-Object -First 20))
    Add-Check ($missingOldFiles.Count -eq 0) 'all old manifest files are installed' "missing=$($missingOldFiles.Count) $missingOldSummary"
    Add-Check ((Test-Path -LiteralPath (Join-Path $shortcutDir 'OpenLnku.lnk')) -and (Test-Path -LiteralPath (Join-Path $shortcutDir 'Uninstall.lnk'))) 'isolated shortcuts installed' ''

    $userFile = Join-Path $installDir 'user-note.txt'
    $userContent = 'User-owned file retained across upgrade and uninstall.'
    [IO.File]::WriteAllText($userFile, $userContent, [Text.UTF8Encoding]::new($false))
    Start-Installer 'setup-new.exe'

    Add-Check (([IO.File]::ReadAllText($markerPath)).Trim() -eq 'OpenLnkuAcceptance') 'upgrade retains ownership marker' ''
    Add-Check ((Get-Item -LiteralPath $appPath).VersionInfo.ProductVersion -eq $version) 'new package version installed' "ProductVersion=$version"
    Add-Check (Test-Path -LiteralPath $readmePath -PathType Leaf) 'new package README installed' ''
    Add-Check (-not (Test-Path -LiteralPath $obsoletePath)) 'upgrade removes old-only managed file' ''
    Add-Check (-not (Test-Path -LiteralPath (Join-Path $installDir '.acceptance-owned'))) 'upgrade removes old-only fixture marker' ''
    Add-Check ([IO.File]::ReadAllText($userFile) -eq $userContent) 'upgrade retains user-owned file' ''
    $newManifest = Read-ManagedManifest $manifestPath
    Add-Check (-not ($newManifest -contains 'obsolete-managed.txt')) 'new manifest no longer owns obsolete file' ''
    $missingNewFiles = @(Get-MissingManagedFiles -Root $installDir -Manifest $newManifest)
    $missingNewSummary = [string]::Join(', ', @($missingNewFiles | Select-Object -First 20))
    Add-Check ($missingNewFiles.Count -eq 0) 'all new manifest files are installed' "missing=$($missingNewFiles.Count) $missingNewSummary"

    Start-Installer 'setup-new.exe'
    Add-Check ((Get-Item -LiteralPath $appPath).VersionInfo.ProductVersion -eq $version) 'repair keeps current executable' ''
    Add-Check ([IO.File]::ReadAllText($userFile) -eq $userContent) 'repair retains user-owned file' ''
    $repairManifest = Read-ManagedManifest $manifestPath
    $missingRepairFiles = @(Get-MissingManagedFiles -Root $installDir -Manifest $repairManifest)
    $missingRepairSummary = [string]::Join(', ', @($missingRepairFiles | Select-Object -First 20))
    Add-Check ($missingRepairFiles.Count -eq 0) 'all manifest files remain after repair' "missing=$($missingRepairFiles.Count) $missingRepairSummary"
    $shortcutFiles = @(Get-ChildItem -LiteralPath $shortcutDir -Filter '*.lnk' -File)
    Add-Check ($shortcutFiles.Count -eq 2) 'repair keeps a single pair of managed shortcuts' "count=$($shortcutFiles.Count)"
    $managedDirectoriesBeforeUninstall = @(Get-ChildItem -LiteralPath $installDir -Directory -Recurse -Force)
    Add-Check ($managedDirectoriesBeforeUninstall.Count -gt 0) 'package includes managed subdirectories' "count=$($managedDirectoriesBeforeUninstall.Count)"

    $readmeLock = [IO.File]::Open($readmePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    Start-Uninstaller -ExpectedExitCode 20
    Add-Check (Test-Path -LiteralPath $markerPath -PathType Leaf) 'failed uninstall retains retry marker' ''
    Add-Check (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'failed uninstall retains retry manifest' ''
    Add-Check (Test-Path -LiteralPath $readmePath -PathType Leaf) 'locked README remains after failed uninstall' ''
    $readmeLock.Dispose()
    $readmeLock = $null

    Start-Uninstaller
    Add-Check (-not (Test-Path -LiteralPath $appPath)) 'retry removes managed executable' ''
    Add-Check (-not (Test-Path -LiteralPath $readmePath)) 'retry removes released README' ''
    Add-Check (-not (Test-Path -LiteralPath $manifestPath) -and -not (Test-Path -LiteralPath $markerPath)) 'retry removes management metadata' ''
    Add-Check (-not (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall.exe'))) 'retry removes installed uninstaller' ''
    Add-Check ([IO.File]::ReadAllText($userFile) -eq $userContent) 'uninstall preserves user-owned file' ''
    $remainingDirectories = @(Get-ChildItem -LiteralPath $installDir -Directory -Recurse -Force)
    Add-Check ($remainingDirectories.Count -eq 0) 'uninstall removes empty managed directories' "remaining=$($remainingDirectories.Count)"
    Add-Check (-not (Test-Path -LiteralPath $shortcutDir)) 'uninstall removes isolated shortcuts directory' ''

    Remove-Item -LiteralPath $userFile -Force
    Add-Check ((Get-ChildItem -LiteralPath $installDir -Force | Measure-Object).Count -eq 0) 'only the empty install root remains after user-file removal' ''
    [IO.Directory]::Delete($installDir, $false)
    Add-Check (-not (Test-Path -LiteralPath $installDir)) 'empty isolated install root can be removed' ''

    $overall = 'Passed'
} catch {
    $overall = 'Failed'
    $failure = $_.Exception.Message
    throw
} finally {
    if ($readmeLock) { $readmeLock.Dispose() }
    $report = [ordered]@{
        schemaVersion = 1
        test = 'isolated-installer-files-only'
        identity = 'OpenLnkuAcceptance'
        startedUtc = $startedUtc.ToString('o')
        finishedUtc = [DateTime]::UtcNow.ToString('o')
        overall = $overall
        failure = $failure
        scope = [ordered]@{
            installRoot = $installDir
            acceptanceRoot = $testRoot
            registryTested = $false
            registryNote = 'Compiled ISOLATED_FILES_ONLY installers skip registry writes and cleanup. This result makes no registry or preference claim.'
            authenticationTested = $false
            clientProcessStarted = $false
        }
        checks = @($results)
        logs = if (Test-Path -LiteralPath $logDir -PathType Container) { $logDir } else { $null }
    }
    [IO.File]::WriteAllText($resultPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}

Write-Output "Isolated file-only installer acceptance passed. Registry behavior was not tested. Report: $resultPath"
