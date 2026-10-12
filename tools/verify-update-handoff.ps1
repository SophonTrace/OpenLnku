#requires -Version 7.4
# Full parent validation/wait/replacement/restart using a harmless fixture.
# No real client, network, credentials, settings or registry are accessed.
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$run = Join-Path $workspace ('.deps\handoff-acceptance-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
# The Codex workspace can inherit Low integrity. The fixture must model a
# normal Explorer launch, not lower the helper's token by its executable label.
$rid = & (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')
if ($rid.integrityRID -lt 8192) { throw 'Run acceptance from a normal user PowerShell.' }
$acl = Get-Acl -LiteralPath $run
$access = [Security.AccessControl.AccessControlSections]::Access
$saved = $acl.GetSecurityDescriptorSddlForm($access)
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
if ($acl.GetOwner([Security.Principal.SecurityIdentifier]) -ne $sid) { throw 'Acceptance directory owner mismatch.' }
try {
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid,'TakeOwnership','Allow'))
    Set-Acl -LiteralPath $run -AclObject $acl
    & "$env:WINDIR\System32\icacls.exe" $run /setintegritylevel '(OI)(CI)M' | Out-Null
    if ($LASTEXITCODE) { throw 'Unable to set the isolated acceptance directory to Medium.' }
} finally { $restore=Get-Acl -LiteralPath $run; $restore.SetSecurityDescriptorSddlForm($saved,$access); Set-Acl -LiteralPath $run -AclObject $restore }
$version = (Get-Content -LiteralPath (Join-Path $workspace 'VERSION.txt') -Raw).Trim()
$fixture = Join-Path $workspace 'build\update_handoff_fixture.exe'
$oldFixture = Join-Path $workspace 'build\update_handoff_old_fixture.exe'
$helper = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'apply-update.ps1'),[Text.Encoding]::UTF8)
$utf8 = [Text.UTF8Encoding]::new($false)
$results = @()
function WriteText($path,$text) { [IO.File]::WriteAllText($path,$text,$utf8) }
function Hash($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert($value,$message) { if (-not $value) { throw $message } }
function Prepare($name) {
    $root = Join-Path $run $name
    $app = Join-Path $root '应用 空格'
    $local = Join-Path $root 'local'
    $cache = Join-Path $local ('OpenLnku\OpenLnku\Updates\' + [guid]::NewGuid().ToString())
    $payload = Join-Path $root "OpenLnku-$version-windows-x64"
    foreach ($dir in @($app,$cache,$payload)) { [IO.Directory]::CreateDirectory($dir) | Out-Null }
    Copy-Item -LiteralPath $oldFixture -Destination (Join-Path $app 'OpenLnku.exe')
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $payload 'OpenLnku.exe')
    WriteText (Join-Path $app '.update-acceptance-owned') 'isolated fixture'
    WriteText (Join-Path $app 'obsolete.txt') 'old managed file'
    WriteText (Join-Path $app 'private-user.txt') 'must be preserved'
    WriteText (Join-Path $app 'payload-manifest.json') (@{version='1.2.0';files=@(@{path='OpenLnku.exe'},@{path='obsolete.txt'})}|ConvertTo-Json -Depth 4 -Compress)
    WriteText (Join-Path $payload 'new.txt') 'new managed file'
    $files = @(Get-ChildItem -LiteralPath $payload -File | ForEach-Object { @{path=$_.Name;bytes=$_.Length;sha256=(Hash $_.FullName)} })
    WriteText (Join-Path $payload 'payload-manifest.json') (@{version=$version;files=$files}|ConvertTo-Json -Depth 4 -Compress)
    $archive = Join-Path $cache 'package.zip'
    Compress-Archive -LiteralPath $payload -DestinationPath $archive
    $job = @{testOnly=$true;testRestart=$true;parentId=0;appRoot=$app;jobRoot=$cache;version=$version;archive=$archive;bytes=(Get-Item -LiteralPath $archive).Length;sha256=(Hash $archive)}
    WriteText (Join-Path $app 'fixture-helper.ps1') ("Start-Sleep -Milliseconds 900`n" + $helper)
    return @{root=$root;app=$app;local=$local;cache=$cache;job=$job}
}
function RunFixture($case, $ExpectedVersion = $version) {
    WriteText (Join-Path $case.app 'fixture-job.json') ($case.job|ConvertTo-Json -Depth 4 -Compress)
    $process = Start-Process -FilePath (Join-Path $case.app 'OpenLnku.exe') -WorkingDirectory $case.app -WindowStyle Hidden -PassThru -Environment @{
        LOCALAPPDATA=$case.local
        PATH=(Join-Path $workspace '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
        PSModulePath='nonexistent-Core-module-path'
    } -RedirectStandardOutput (Join-Path $case.root 'parent.stdout.log') -RedirectStandardError (Join-Path $case.root 'parent.stderr.log')
    try {
        Start-Sleep -Milliseconds 400
        $process.Refresh(); Assert (-not $process.HasExited) 'Parent exited before delayed helper was ready.'
        Assert ($process.WaitForExit(20000)) 'Fixture parent did not finish.'
        Assert ($process.ExitCode -eq 0) ('Fixture parent failed: ' + $process.ExitCode)
        Assert (Test-Path -LiteralPath (Join-Path $case.app 'parent-acknowledged')) 'No parent readiness acknowledgement.'
        $deadline = [DateTime]::UtcNow.AddSeconds(25)
        while (-not (Test-Path -LiteralPath (Join-Path $case.app 'restarted.json')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        Assert (Test-Path -LiteralPath (Join-Path $case.app 'restarted.json')) 'Replacement/original fixture did not restart.'
        $restart = Get-Content -LiteralPath (Join-Path $case.app 'restarted.json') -Raw | ConvertFrom-Json
        Assert ($restart.pid -ne $process.Id -and $restart.version -eq $ExpectedVersion) 'Restart did not launch the expected old/new fixture executable.'
        return (Get-Content -LiteralPath (Join-Path $case.cache 'result.json') -Raw | ConvertFrom-Json)
    } finally { $process.Refresh(); if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
}
$success = Prepare 'success'
$outcome = RunFixture $success
Assert ($outcome.status -eq 'success') ('Update failed: ' + $outcome.message)
Assert ((Test-Path -LiteralPath (Join-Path $success.app 'new.txt')) -and -not (Test-Path -LiteralPath (Join-Path $success.app 'obsolete.txt'))) 'Managed files were not replaced.'
Assert ((Get-Content -LiteralPath (Join-Path $success.app 'private-user.txt') -Raw) -eq 'must be preserved') 'User file was changed.'
$results += @{case='delayed helper, Unicode/space path, replacement and automatic restart';passed=$true}
$rollback = Prepare 'rollback'
$oldHash = Hash (Join-Path $rollback.app 'OpenLnku.exe')
# new.txt, obsolete.txt, payload-manifest.json, then OpenLnku.exe last.
$rollback.job.failAfter = 4
$outcome = RunFixture $rollback '1.2.0'
Assert ($outcome.status -eq 'failed' -and $outcome.message -match '已恢复旧版') 'Rollback result missing.'
Assert ((Test-Path -LiteralPath (Join-Path $rollback.app 'obsolete.txt')) -and -not (Test-Path -LiteralPath (Join-Path $rollback.app 'new.txt'))) 'Rollback did not restore managed files.'
Assert ((Hash (Join-Path $rollback.app 'OpenLnku.exe')) -eq $oldHash) 'Rollback did not restore the old executable byte for byte.'
$results += @{case='failure after executable replacement restores old executable and restarts version 1.2.0';passed=$true}
$invalid = Prepare 'bad-hash'
$invalid.job.sha256 = '0' * 64
$outcome = RunFixture $invalid '1.2.0'
Assert ($outcome.status -eq 'failed' -and $outcome.message -match 'integrity mismatch') 'Invalid archive was not rejected.'
Assert (Test-Path -LiteralPath (Join-Path $invalid.app 'obsolete.txt')) 'Original files were lost on pre-apply failure.'
$results += @{case='invalid archive retains and restarts original fixture';passed=$true}
$conflict = Prepare 'helper-not-ready'
$installer = [Threading.Mutex]::new($false,'Local\OpenLnkuAcceptanceUpdate.Installer')
try {
    WriteText (Join-Path $conflict.app 'fixture-job.json') ($conflict.job|ConvertTo-Json -Depth 4 -Compress)
    $parent = Start-Process -FilePath (Join-Path $conflict.app 'OpenLnku.exe') -WorkingDirectory $conflict.app -WindowStyle Hidden -PassThru -Environment @{
        LOCALAPPDATA=$conflict.local;PATH=(Join-Path $workspace '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
    }
    try {
        Assert ($parent.WaitForExit(20000)) 'Failed-readiness fixture timed out.'
        Assert ($parent.ExitCode -eq 96) 'Preparation failure did not reach the still-running parent.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $conflict.app 'parent-acknowledged'))) 'Client exited for installation without readiness.'
        Assert ((Get-Content -LiteralPath (Join-Path $conflict.app 'parent-failure') -Raw) -match 'Another installer') 'Preparation failure was not explained.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $conflict.cache 'stage'))) 'Unready helper changed application files.'
    } finally { $parent.Refresh(); if (-not $parent.HasExited) { $parent.Kill(); $parent.WaitForExit() }; $parent.Dispose() }
} finally { $installer.Dispose() }
$results += @{case='helper preflight failure reports to parent without installation exit';passed=$true}
$cancel = Prepare 'cancelled-handoff'
$cancel.job.testPauseAfterReadyMs = 3000
WriteText (Join-Path $cancel.app 'fixture-job.json') ($cancel.job|ConvertTo-Json -Depth 4 -Compress)
$parent = Start-Process -FilePath (Join-Path $cancel.app 'OpenLnku.exe') -WorkingDirectory $cancel.app -WindowStyle Hidden -PassThru -Environment @{
    LOCALAPPDATA=$cancel.local;PATH=(Join-Path $workspace '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $ack = $null
    while (-not $ack -and [DateTime]::UtcNow -lt $deadline) { $ack = Get-ChildItem -LiteralPath $cancel.cache -Filter 'ready-*.json' | Select-Object -First 1; if (-not $ack) { Start-Sleep -Milliseconds 100 } }
    Assert ($null -ne $ack) 'Cancellable helper never became ready.'
    $token = (Get-Content -LiteralPath $ack.FullName -Raw | ConvertFrom-Json).launchToken
    WriteText (Join-Path $cancel.cache "cancel-$token") ''
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not (Test-Path -LiteralPath (Join-Path $cancel.cache 'result.json')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    $outcome = Get-Content -LiteralPath (Join-Path $cancel.cache 'result.json') -Raw | ConvertFrom-Json
    Assert ($outcome.status -eq 'failed' -and $outcome.message -eq 'Update launch cancelled.') 'Helper did not observe cancellation while waiting for the parent.'
    $parent.Refresh(); Assert (-not $parent.HasExited) 'Cancellation required the client to exit.'
    $active = $null; Assert (-not [Threading.Mutex]::TryOpenExisting('Local\OpenLnkuAcceptanceUpdate.Installer',[ref]$active)) 'Cancelled helper retained the installer mutex.'
    Assert ($parent.WaitForExit(10000)) 'Cancelled fixture parent did not finish.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $cancel.cache 'stage'))) 'Cancelled helper started installing after a later parent exit.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $cancel.app 'restarted.json'))) 'Cancelled helper restarted the fixture.'
} finally { $parent.Refresh(); if (-not $parent.HasExited) { $parent.Kill(); $parent.WaitForExit() }; $parent.Dispose() }
$results += @{case='cancelled handoff promptly releases lock and never applies after a later exit';passed=$true}
# Reproduce the published 1.2.0 helper's race deterministically: its parent
# has already exited before the helper performs GetProcessById. No old app
# or authentication code is executed.
$legacy = Prepare 'legacy-parent-gone'
Remove-Item -LiteralPath (Join-Path $legacy.app 'fixture-helper.ps1')
$parent = Start-Process -FilePath (Join-Path $legacy.app 'OpenLnku.exe') -WorkingDirectory $legacy.app -WindowStyle Hidden -PassThru -Environment @{
    PATH=(Join-Path $workspace '.deps\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
}
Assert ($parent.WaitForExit(10000)) 'Legacy race parent did not exit.'
$legacy.job.Remove('testOnly'); $legacy.job.Remove('testRestart'); $legacy.job.parentId = $parent.Id
$oldHelper = (& git -C $workspace show 'v1.2.0:tools/apply-update.ps1') -join "`n"
Assert ($LASTEXITCODE -eq 0) 'Published 1.2.0 helper source was not available.'
$oldPath = Join-Path $legacy.root 'legacy-helper.ps1'; WriteText $oldPath $oldHelper
$json64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($legacy.job|ConvertTo-Json -Depth 4 -Compress)))
$path64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($oldPath))
$wrapper = '$OpenLnkuJobJson=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $json64 + ''')); '
$wrapper += '& ([ScriptBlock]::Create([IO.File]::ReadAllText([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $path64 + ''')),[Text.Encoding]::UTF8)))'
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($wrapper))
$oldProcess = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList @('-NoProfile','-NonInteractive','-WindowStyle','Hidden','-EncodedCommand',$encoded) -WorkingDirectory $legacy.cache -WindowStyle Hidden -PassThru -Environment @{
    LOCALAPPDATA=$legacy.local;PSModulePath=(Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules')
} -RedirectStandardOutput (Join-Path $legacy.root 'legacy.stdout.log') -RedirectStandardError (Join-Path $legacy.root 'legacy.stderr.log')
try {
    Assert ($oldProcess.WaitForExit(10000)) 'Old helper did not finish.'
    Assert ($oldProcess.ExitCode -ne 0) 'Old helper unexpectedly accepted an exited parent.'
    $outcome = Get-Content -LiteralPath (Join-Path $legacy.cache 'result.json') -Raw | ConvertFrom-Json
    Assert ($outcome.status -eq 'failed') 'Old helper did not write its parent lookup failure.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $legacy.app 'restarted.json'))) 'Old helper unexpectedly restarted the fixture.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $legacy.cache 'stage'))) 'Old helper unexpectedly started installing.'
} finally { $parent.Dispose(); $oldProcess.Refresh(); if (-not $oldProcess.HasExited) { $oldProcess.Kill(); $oldProcess.WaitForExit() }; $oldProcess.Dispose() }
$results += @{case='published 1.2.0 exited-parent failure reproduces without restart';passed=$true}
WriteText (Join-Path $run 'report.json') ($results|ConvertTo-Json -Depth 4)
$results | ConvertTo-Json -Depth 4
Write-Output "Isolated handoff acceptance passed. Local report: $run\report.json"
