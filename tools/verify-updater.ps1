# Offline acceptance for tools/apply-update.ps1. Run from Windows PowerShell 5.1.
# Every mutation is confined to a new, GUID-named .deps/update-acceptance tree.
# This script never launches OpenLnku.exe and never reads or writes HKCU.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$releaseVersion = (Get-Content -LiteralPath (Join-Path $workspace 'VERSION.txt') -Raw).Trim()
$acceptanceParent = Join-Path $workspace '.deps\update-acceptance'
$null = New-Item -ItemType Directory -Path $acceptanceParent -Force
$guid = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $acceptanceParent $guid
$reportPath = Join-Path $workspace ("docs\update-acceptance-{0}.json" -f $guid)
$helperPath = Join-Path $PSScriptRoot 'apply-update.ps1'
$oldExePath = Join-Path $workspace 'dist\OpenNex-1.0.1-windows-x64\OpenNex.exe'
$newExePath = Join-Path $workspace 'build\OpenLnku.exe'
$powerShell51 = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$checks = New-Object 'Collections.Generic.List[object]'
$cases = New-Object 'Collections.Generic.List[object]'
$fatal = $null

function Assert-PlainAncestors([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing reparse point in acceptance path: $current"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if (-not $parent -or $parent -eq $current) { break }
        $current = $parent
    }
}

function Add-Check([string]$Case, [bool]$Passed, [string]$Name, [string]$Detail = '') {
    $script:checks.Add([pscustomobject]@{ case = $Case; check = $Name; passed = $Passed; detail = $Detail })
    Write-Output ("[{0}] {1}: {2}{3}" -f $Case, $Name, $Passed, $(if ($Detail) { " ($Detail)" } else { '' }))
}

function Invoke-Case([string]$Name, [scriptblock]$Body) {
    $before = $script:checks.Count
    try { & $Body }
    catch { Add-Check $Name $false 'case-exception' $_.Exception.Message }
    $passed = $true
    for ($i = $before; $i -lt $script:checks.Count; $i++) {
        if (-not $script:checks[$i].passed) { $passed = $false }
    }
    $script:cases.Add([pscustomobject]@{ name = $Name; passed = $passed })
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

function Write-Bytes([string]$Path, [byte[]]$Bytes) {
    $parent = [IO.Path]::GetDirectoryName($Path)
    if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

function Get-Sha256Bytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Snapshot-Files([string]$Directory) {
    $snapshot = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Directory -File -Recurse -Force) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Unexpected reparse point in app tree: $($file.FullName)" }
        $relative = $file.FullName.Substring($Directory.Length).TrimStart('\')
        $snapshot[$relative] = [IO.File]::ReadAllBytes($file.FullName)
    }
    return ,$snapshot
}

function Compare-Snapshots($Expected, $Actual) {
    if ($Expected.Count -ne $Actual.Count) { return $false }
    foreach ($name in $Expected.Keys) {
        if (-not $Actual.ContainsKey($name)) { return $false }
        $left = [byte[]]$Expected[$name]
        $right = [byte[]]$Actual[$name]
        if ($left.Length -ne $right.Length) { return $false }
        for ($i = 0; $i -lt $left.Length; $i++) { if ($left[$i] -ne $right[$i]) { return $false } }
    }
    return $true
}

function Snapshot-Summary($Snapshot) {
    $items = @()
    foreach ($name in @($Snapshot.Keys | Sort-Object)) {
        $bytes = [byte[]]$Snapshot[$name]
        $items += [pscustomobject]@{ path = $name; bytes = $bytes.Length; sha256 = Get-Sha256Bytes $bytes }
    }
    return ,$items
}

function New-Scenario([string]$Name) {
    $root = Join-Path $script:runRoot $Name
    $app = Join-Path $root '安装 空间'
    $cache = Join-Path $root 'cache'
    $temp = Join-Path $root 'temp'
    [IO.Directory]::CreateDirectory($app) | Out-Null
    [IO.Directory]::CreateDirectory($cache) | Out-Null
    [IO.Directory]::CreateDirectory($temp) | Out-Null

    $oldExeBytes = [IO.File]::ReadAllBytes($script:oldExePath)
    Write-Bytes (Join-Path $app 'OpenLnku.exe') $oldExeBytes
    Write-Utf8NoBom (Join-Path $app '.update-acceptance-owned') 'isolated acceptance fixture'
    Write-Utf8NoBom (Join-Path $app '.opennex-installed') 'OpenLnku'
    Write-Bytes (Join-Path $app 'obsolete.txt') ([Text.Encoding]::UTF8.GetBytes('old managed file'))
    Write-Bytes (Join-Path $app 'user-note.txt') ([Text.Encoding]::UTF8.GetBytes('user-owned bytes, keep exactly'))
    Write-Bytes (Join-Path $app 'user-data.bin') ([byte[]](0, 1, 2, 10, 13, 255))
    $oldPayload = [ordered]@{
        version = '1.0.1'
        files = @([ordered]@{ path = 'OpenLnku.exe'; bytes = $oldExeBytes.Length; sha256 = Get-Sha256Bytes $oldExeBytes },
                  [ordered]@{ path = 'obsolete.txt'; bytes = [Text.Encoding]::UTF8.GetByteCount('old managed file'); sha256 = Get-Sha256Bytes ([Text.Encoding]::UTF8.GetBytes('old managed file')) })
    }
    Write-Utf8NoBom (Join-Path $app 'payload-manifest.json') (ConvertTo-Json -InputObject $oldPayload -Depth 5 -Compress)
    Write-Utf8NoBom (Join-Path $app '.opennex-files.txt') "OpenLnku.exe`r`nobsolete.txt`r`npayload-manifest.json`r`n"

    return [pscustomobject]@{ name = $Name; root = $root; app = $app; cache = $cache; temp = $temp }
}

function Add-ZipBytes($Archive, [string]$EntryName, [byte[]]$Bytes) {
    $entry = $Archive.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Fastest)
    $stream = $entry.Open()
    try { $stream.Write($Bytes, 0, $Bytes.Length) }
    finally { $stream.Dispose() }
}

function New-Package($Scenario, [switch]$Traversal, [switch]$Duplicate, [switch]$Conflict) {
    $newExeBytes = [IO.File]::ReadAllBytes($script:newExePath)
    $newData = [Text.Encoding]::UTF8.GetBytes('new managed payload v1.1.0')
    $files = New-Object 'Collections.Generic.List[object]'
    $files.Add([ordered]@{ path = 'OpenLnku.exe'; bytes = $newExeBytes.Length; sha256 = Get-Sha256Bytes $newExeBytes })
    $files.Add([ordered]@{ path = 'new-file.txt'; bytes = $newData.Length; sha256 = Get-Sha256Bytes $newData })
    if ($Conflict) {
        $conflictBytes = [Text.Encoding]::UTF8.GetBytes('must not overwrite user data')
        $files.Add([ordered]@{ path = 'user-note.txt'; bytes = $conflictBytes.Length; sha256 = Get-Sha256Bytes $conflictBytes })
    }
    $payload = [ordered]@{ version = $script:releaseVersion; files = @($files.ToArray()) }
    $payloadBytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $payload -Depth 6 -Compress))
     $prefix = "OpenLnku-$script:releaseVersion-windows-x64/"
    $zipPath = Join-Path $Scenario.cache 'package.zip'
    $file = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $zip = New-Object IO.Compression.ZipArchive($file, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        Add-ZipBytes $zip ($prefix + 'OpenLnku.exe') $newExeBytes
        Add-ZipBytes $zip ($prefix + 'new-file.txt') $newData
        if ($Duplicate) { Add-ZipBytes $zip ($prefix + 'new-file.txt') $newData }
        if ($Conflict) { Add-ZipBytes $zip ($prefix + 'user-note.txt') $conflictBytes }
        Add-ZipBytes $zip ($prefix + 'payload-manifest.json') $payloadBytes
        if ($Traversal) { Add-ZipBytes $zip ($prefix + '../escape.txt') ([Text.Encoding]::UTF8.GetBytes('must not escape stage')) }
    } finally { $zip.Dispose(); $file.Dispose() }

    return [pscustomobject]@{ path = $zipPath; bytes = (Get-Item -LiteralPath $zipPath).Length; sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() }
}

function New-ApplyingJournal($Scenario, [string[]]$Changed, [string[]]$Old, [switch]$OmitBackup) {
    if (-not $OmitBackup) {
        foreach ($name in $Changed) {
            if ($Old -contains $name) {
                $source = Join-Path $Scenario.app $name
                $destination = Join-Path (Join-Path $Scenario.cache 'backup') $name
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
                [IO.File]::Copy($source, $destination, $false)
            }
        }
    }
    $journal = [ordered]@{ state = 'applying'; app = $Scenario.app; changed = @($Changed); old = @($Old) }
    Write-Utf8NoBom (Join-Path $Scenario.cache 'journal.json') (ConvertTo-Json -InputObject $journal -Depth 5 -Compress)
}

function Corrupt-ManagedExe($Scenario) {
    Write-Bytes (Join-Path $Scenario.app 'OpenLnku.exe') ([byte[]](0x4d, 0x5a, 0x00, 0x01, 0x02, 0x03, 0x04))
}

function Invoke-Helper($Scenario, $Archive, [hashtable]$Overrides = @{}) {
    $job = [ordered]@{
        testOnly = $true
        parentId = 0
        appRoot = $Scenario.app
        jobRoot = $Scenario.cache
        version = $script:releaseVersion
        archive = $Archive.path
        sha256 = $Archive.sha256
        bytes = $Archive.bytes
    }
    foreach ($key in $Overrides.Keys) { $job[$key] = $Overrides[$key] }
    $jobJson = ConvertTo-Json -InputObject $job -Depth 5 -Compress
    $jobPath = Join-Path $Scenario.root 'job.json'
    Write-Utf8NoBom $jobPath $jobJson
    $jobB64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($jobJson))
    $helperB64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script:helperPath))
    $wrapper = '$ErrorActionPreference = ''Stop''; if ($PSVersionTable.PSVersion.Major -ne 5) { exit 90 }; '
    $wrapper += '$OpenLnkuJobJson = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $jobB64 + ''')); '
    $wrapper += '$OpenLnkuHelperPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $helperB64 + ''')); '
    $wrapper += '& ([ScriptBlock]::Create([IO.File]::ReadAllText($OpenLnkuHelperPath, [Text.Encoding]::UTF8))); exit 0'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($wrapper))
    [IO.File]::WriteAllText((Join-Path $Scenario.root 'powershell-encoded-command.txt'), $encoded, (New-Object Text.UTF8Encoding($false)))
    $stdout = Join-Path $Scenario.root 'helper.stdout.txt'
    $stderr = Join-Path $Scenario.root 'helper.stderr.txt'

    $processInfo = New-Object Diagnostics.ProcessStartInfo
    $processInfo.FileName = $script:powerShell51
    $processInfo.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ' + $encoded
    $processInfo.WorkingDirectory = $Scenario.root
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.EnvironmentVariables['TEMP'] = $Scenario.temp
    $processInfo.EnvironmentVariables['TMP'] = $Scenario.temp
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $processInfo
    if (-not $process.Start()) { throw 'Failed to start Windows PowerShell 5.1 helper process.' }
    $outTask = $process.StandardOutput.ReadToEndAsync()
    $errTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        try { $process.Kill() } catch {}
        $process.WaitForExit()
        throw 'Windows PowerShell helper exceeded 120 seconds and was terminated.'
    }
    $process.WaitForExit()
    $outText = $outTask.GetAwaiter().GetResult()
    $errText = $errTask.GetAwaiter().GetResult()
    Write-Utf8NoBom $stdout $outText
    Write-Utf8NoBom $stderr $errText
    $resultPath = Join-Path $Scenario.cache 'result.json'
    $result = $null
    if (Test-Path -LiteralPath $resultPath) { $result = [IO.File]::ReadAllText($resultPath, [Text.Encoding]::UTF8) | ConvertFrom-Json }
    return [pscustomobject]@{ exitCode = $process.ExitCode; result = $result; stdout = $stdout; stderr = $stderr; job = $jobPath }
}

function Assert-RejectedWithoutChanges([string]$CaseName, $Scenario, $Archive, [hashtable]$Overrides = @{}, [switch]$Traversal) {
    if ($Traversal) { Write-Utf8NoBom (Join-Path $Scenario.cache 'escape.txt') 'guard bytes stay here' }
    $before = Snapshot-Files $Scenario.app
    $run = Invoke-Helper $Scenario $Archive $Overrides
    Add-Check $CaseName ($run.exitCode -ne 0 -and $run.result -and $run.result.status -eq 'failed') 'helper-rejected' $(if ($run.result) { $run.result.message } else { 'no result.json' })
    $after = Snapshot-Files $Scenario.app
    Add-Check $CaseName (Compare-Snapshots $before $after) 'old-install-byte-identical'
    if ($Traversal) {
        $guard = Join-Path $Scenario.cache 'escape.txt'
        Add-Check $CaseName ((Test-Path -LiteralPath $guard) -and [IO.File]::ReadAllText($guard) -ceq 'guard bytes stay here') 'traversal-guard-unchanged'
    }
}

try {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Assert-PlainAncestors $acceptanceParent
    Assert-PlainAncestors (Join-Path $workspace 'dist')
    Assert-PlainAncestors (Join-Path $workspace 'build')
    if (Test-Path -LiteralPath $runRoot) { throw "GUID acceptance root unexpectedly exists: $runRoot" }
    if (Test-Path -LiteralPath $reportPath) { throw "Unique report path unexpectedly exists: $reportPath" }
    if (-not (Test-Path -LiteralPath $oldExePath) -or -not (Test-Path -LiteralPath $newExePath)) { throw 'Required old or new EXE is missing.' }
    if (-not (Test-Path -LiteralPath $helperPath)) { throw 'Update helper is missing.' }
    if (-not (Test-Path -LiteralPath $powerShell51)) { throw 'Windows PowerShell 5.1 executable is missing.' }
    $oldVersion = (Get-Item -LiteralPath $oldExePath).VersionInfo.ProductVersion
    $newVersion = (Get-Item -LiteralPath $newExePath).VersionInfo.ProductVersion
    if ($oldVersion -ne '1.0.1') { throw "Expected old package PE ProductVersion 1.0.1, got $oldVersion." }
    if ($newVersion -ne (Get-Content -LiteralPath (Join-Path $workspace 'VERSION.txt') -Raw).Trim()) { throw "Expected current build PE ProductVersion, got $newVersion." }

    [IO.Directory]::CreateDirectory($runRoot) | Out-Null
    Write-Utf8NoBom (Join-Path $runRoot '.update-acceptance-owned') 'Only this GUID acceptance tree is owned by verify-updater.ps1.'
    Write-Output "Acceptance root: $runRoot"
    Write-Output "Report: $reportPath"

    Invoke-Case 'successful-update' {
        $scenario = New-Scenario 'success'
        $archive = New-Package $scenario
        $before = Snapshot-Files $scenario.app
        $run = Invoke-Helper $scenario $archive
        Add-Check 'successful-update' ($run.exitCode -eq 0 -and $run.result -and $run.result.status -eq 'success') 'helper-success' $(if ($run.result) { $run.result.message } else { 'no result.json' })
        if (-not ($run.exitCode -eq 0 -and $run.result -and $run.result.status -eq 'success')) { return }
        $app = $scenario.app
        Add-Check 'successful-update' ((Get-Item -LiteralPath (Join-Path $app 'OpenLnku.exe')).VersionInfo.ProductVersion -eq $newVersion) 'new-exe-version'
        Add-Check 'successful-update' (-not (Test-Path -LiteralPath (Join-Path $app 'obsolete.txt'))) 'obsolete-managed-file-removed'
        Add-Check 'successful-update' ((Test-Path -LiteralPath (Join-Path $app 'new-file.txt')) -and [IO.File]::ReadAllText((Join-Path $app 'new-file.txt')) -ceq 'new managed payload v1.1.0') 'new-managed-file-installed'
        Add-Check 'successful-update' ([IO.File]::ReadAllText((Join-Path $app 'user-note.txt')) -ceq 'user-owned bytes, keep exactly') 'unknown-user-file-preserved'
        Add-Check 'successful-update' (([IO.File]::ReadAllBytes((Join-Path $app 'user-data.bin')) -join ',') -ceq '0,1,2,10,13,255') 'binary-user-file-preserved'
        $newManifest = [IO.File]::ReadAllText((Join-Path $app '.opennex-files.txt'), [Text.Encoding]::UTF8) -split "`r?`n" | Where-Object { $_ }
        $expectedManifest = @('OpenLnku.exe', 'new-file.txt', 'payload-manifest.json')
        Add-Check 'successful-update' (@(Compare-Object ($newManifest | Sort-Object) ($expectedManifest | Sort-Object)).Count -eq 0) 'installed-managed-manifest-updated' ($newManifest -join ',')
        Add-Check 'successful-update' (([IO.File]::ReadAllText((Join-Path $app '.opennex-installed')) -ceq 'OpenLnku') -and (Test-Path -LiteralPath (Join-Path $app '.update-acceptance-owned'))) 'acceptance-marker-retained'
        $after = Snapshot-Files $app
        Add-Check 'successful-update' ((Compare-Snapshots $before $after) -eq $false) 'install-tree-changed'
    }

    Invoke-Case 'injected-failure-rolls-back' {
        $scenario = New-Scenario 'rollback'
        $archive = New-Package $scenario
        $before = Snapshot-Files $scenario.app
        $run = Invoke-Helper $scenario $archive @{ failAfter = 2 }
        Add-Check 'injected-failure-rolls-back' ($run.exitCode -ne 0 -and $run.result -and $run.result.status -eq 'failed') 'helper-failure-returned' $(if ($run.result) { $run.result.message } else { 'no result.json' })
        Add-Check 'injected-failure-rolls-back' ($run.result -and $run.result.message -match 'Injected acceptance failure') 'failAfter-injection-reached' $(if ($run.result) { $run.result.message } else { 'no result.json' })
        $after = Snapshot-Files $scenario.app
        Add-Check 'injected-failure-rolls-back' (Compare-Snapshots $before $after) 'rollback-restored-every-file-byte-for-byte'
    }

    Invoke-Case 'bad-archive-hash-rejected' {
        $scenario = New-Scenario 'bad-hash'
        $archive = New-Package $scenario
        Assert-RejectedWithoutChanges 'bad-archive-hash-rejected' $scenario $archive @{ sha256 = ('0' * 64) }
    }

    Invoke-Case 'zip-traversal-rejected' {
        $scenario = New-Scenario 'traversal'
        $archive = New-Package $scenario -Traversal
        Assert-RejectedWithoutChanges 'zip-traversal-rejected' $scenario $archive @{} -Traversal
    }

    Invoke-Case 'duplicate-entry-rejected' {
        $scenario = New-Scenario 'duplicate'
        $archive = New-Package $scenario -Duplicate
        Assert-RejectedWithoutChanges 'duplicate-entry-rejected' $scenario $archive
    }

    Invoke-Case 'unknown-file-conflict-rejected' {
        $scenario = New-Scenario 'unknown-conflict'
        $archive = New-Package $scenario -Conflict
        Assert-RejectedWithoutChanges 'unknown-file-conflict-rejected' $scenario $archive
    }

    Invoke-Case 'interrupted-journal-restores-before-archive-validation' {
        $scenario = New-Scenario 'recover-journal'
        $archive = New-Package $scenario
        $before = Snapshot-Files $scenario.app
        New-ApplyingJournal $scenario @('OpenLnku.exe') @('OpenLnku.exe')
        Corrupt-ManagedExe $scenario
        # A recovery must work without relying on a previously downloaded ZIP.
        [IO.File]::Delete($archive.path)
        $run = Invoke-Helper $scenario $archive
        Add-Check 'interrupted-journal-restores-before-archive-validation' ($run.exitCode -eq 0 -and $run.result -and $run.result.status -eq 'success') 'recovery-helper-success' $(if ($run.result) { $run.result.message } else { 'no result.json' })
        Add-Check 'interrupted-journal-restores-before-archive-validation' (Compare-Snapshots $before (Snapshot-Files $scenario.app)) 'old-install-restored-byte-for-byte'
        $journalPath = Join-Path $scenario.cache 'journal.json'
        $journalState = if (Test-Path -LiteralPath $journalPath) { ([IO.File]::ReadAllText($journalPath, [Text.Encoding]::UTF8) | ConvertFrom-Json).state } else { '' }
        Add-Check 'interrupted-journal-restores-before-archive-validation' ($journalState -eq 'recovered') 'journal-marked-recovered' $journalState
    }

    Invoke-Case 'missing-recovery-backup-refused' {
        $scenario = New-Scenario 'recover-missing-backup'
        $archive = New-Package $scenario
        New-ApplyingJournal $scenario @('OpenLnku.exe') @('OpenLnku.exe') -OmitBackup
        Corrupt-ManagedExe $scenario
        [IO.File]::Delete($archive.path)
        $corruptBefore = Snapshot-Files $scenario.app
        $run = Invoke-Helper $scenario $archive
        Add-Check 'missing-recovery-backup-refused' ($run.exitCode -ne 0 -and $run.result -and $run.result.status -eq 'failed') 'recovery-refused' $(if ($run.result) { $run.result.message } else { 'no result.json' })
        Add-Check 'missing-recovery-backup-refused' ($run.result -and $run.result.message -match 'restore|recover|recovery|恢复') 'failure-identifies-recovery'
        Add-Check 'missing-recovery-backup-refused' (Compare-Snapshots $corruptBefore (Snapshot-Files $scenario.app)) 'partial-install-left-untouched-on-refusal'
        Add-Check 'missing-recovery-backup-refused' (Test-Path -LiteralPath (Join-Path $scenario.cache 'result.json')) 'helper-wrote-result'
    }
} catch {
    $fatal = $_.Exception.Message
    Write-Error $fatal
} finally {
    $allPassed = ($null -eq $fatal)
    foreach ($case in $cases) { if (-not $case.passed) { $allPassed = $false } }
    $failedChecks = @($checks | Where-Object { -not $_.passed })
    if ($checks.Count -eq 0 -or $cases.Count -ne 8) { $allPassed = $false }
    $report = [ordered]@{
        schema = 1
        completedUtc = [DateTime]::UtcNow.ToString('o')
        passed = $allPassed
        powershell = $PSVersionTable.PSVersion.ToString()
        oldPeProductVersion = if (Test-Path -LiteralPath $oldExePath) { (Get-Item -LiteralPath $oldExePath).VersionInfo.ProductVersion } else { $null }
        newPeProductVersion = if (Test-Path -LiteralPath $newExePath) { (Get-Item -LiteralPath $newExePath).VersionInfo.ProductVersion } else { $null }
        root = $runRoot
        runRootMarker = '.update-acceptance-owned'
        cases = @($cases.ToArray())
        checks = @($checks.ToArray())
        failureCount = $failedChecks.Count
        fatal = $fatal
        safety = @('testOnly=true', 'parentId=0', 'no OpenLnku.exe process launched', 'no HKCU access by harness/helper test branch', 'no recursive cleanup performed')
    }
    if ($runRoot -and (Test-Path -LiteralPath $runRoot)) {
        Write-Utf8NoBom $reportPath (ConvertTo-Json -InputObject $report -Depth 10)
        Write-Output "JSON report: $reportPath"
        Write-Output "Preserved evidence tree: $runRoot"
    }
}

if (-not $allPassed) { exit 1 }
Write-Output 'All offline update acceptance checks passed.'
