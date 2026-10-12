# Embedded in OpenLnku; launched with an encoded job, never fetched from a release.
$ErrorActionPreference = 'Stop'
$job = $OpenLnkuJobJson | ConvertFrom-Json
$app = [IO.Path]::GetFullPath($job.appRoot).TrimEnd('\','/')
$cache = [IO.Path]::GetFullPath($job.jobRoot).TrimEnd('\','/')
$stage = Join-Path $cache 'stage'
$backup = Join-Path $cache 'backup'
$journalPath = Join-Path $cache 'journal.json'
$parentProcess = $null
$mutex = $null
$changed = New-Object 'Collections.Generic.List[string]'
$oldFiles = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$restart = $false
$status = 'failed'
$message = ''
$rollbackFailed = $false
$launchToken = [string]$job.launchToken
$cancelPath = $null
function Cancelled { return $cancelPath -and [IO.File]::Exists($cancelPath) }
function WriteOutcome {
    $data = @{status=$status;message=$message;version=$job.version;app=$app;launchToken=$launchToken} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $cache 'result.json'),$data,[Text.UTF8Encoding]::new($false))
    if ($launchToken) { [IO.File]::WriteAllText((Join-Path $cache "result-$launchToken.json"),$data,[Text.UTF8Encoding]::new($false)) }
}
function AssertPlainPath([string]$path) {
    $node = $path
    while ($node) {
        if (Test-Path -LiteralPath $node) { if ((Get-Item -LiteralPath $node -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in update path.' } }
        $next = [IO.Path]::GetDirectoryName($node); if ($next -eq $node) { break }; $node = $next
    }
}
function Relative([string]$name) {
    if (-not $name -or $name.Length -gt 800 -or $name -match '[\\:\x00-\x1f]' -or $name.StartsWith('/') -or $name -match '(^|/)\.\.?(/|$)') { throw 'Unsafe package path.' }
    foreach ($part in $name.Split('/')) { if (-not $part -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') { throw 'Unsupported package path.' } }
    return $name
}
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function AtomicCopy([string]$source,[string]$target) {
    AssertPlainPath $source; AssertPlainPath $target
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $temporary = $target + '.openlnku-new-' + [guid]::NewGuid().ToString('N')
    $previous = $temporary + '.previous'
    try {
        [IO.File]::Copy($source,$temporary,$false)
        if ([IO.File]::Exists($target)) { [IO.File]::Replace($temporary,$target,$previous) }
        else { [IO.File]::Move($temporary,$target) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        if ([IO.File]::Exists($previous)) { [IO.File]::Delete($previous) }
    }
}
function PersistJournal {
    $data = @{state='applying';app=$app;changed=@($changed);old=@($oldFiles)} | ConvertTo-Json -Depth 4 -Compress
    [IO.File]::WriteAllText($journalPath + '.new', $data, [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $journalPath) { [IO.File]::Replace($journalPath + '.new',$journalPath,$journalPath + '.previous'); [IO.File]::Delete($journalPath + '.previous') } else { [IO.File]::Move($journalPath + '.new',$journalPath) }
}
function Rollback {
    for ($i=$changed.Count-1; $i -ge 0; $i--) {
        $relative = $changed[$i]; $target = Join-Path $app $relative; AssertPlainPath $target
        if ($oldFiles.Contains($relative)) { AtomicCopy (Join-Path $backup $relative) $target }
        elseif ([IO.File]::Exists($target)) { [IO.File]::Delete($target) }
    }
}
try {
    AssertPlainPath $app; AssertPlainPath $cache
    if (-not [IO.File]::Exists((Join-Path $app 'OpenLnku.exe')) -or -not [IO.Directory]::Exists($cache)) { throw 'Update target missing.' }
    $testOnly = $job.testOnly -eq $true -and (Test-Path -LiteralPath (Join-Path $app '.update-acceptance-owned'))
    if ($launchToken) {
        if ($launchToken -notmatch '^[0-9a-f-]{36}$') { throw 'Invalid update launch token.' }
        $cancelPath = Join-Path $cache "cancel-$launchToken"
    } elseif (-not $testOnly) { throw 'Missing update launch acknowledgement.' }
    if (-not $testOnly -or [int]$job.parentId -gt 0) {
        $allowed = Join-Path $env:LOCALAPPDATA 'OpenLnku\OpenLnku\Updates'
        if (-not $cache.StartsWith([IO.Path]::GetFullPath($allowed).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Update cache is outside the application cache.' }
        $parentProcess = [Diagnostics.Process]::GetProcessById([int]$job.parentId)
        # Retain a real process handle before acknowledging; PID lookup alone
        # does not retain the process whose executable we just checked.
        $null = $parentProcess.Handle
        if (-not $parentProcess.MainModule.FileName.Equals((Join-Path $app 'OpenLnku.exe'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected parent process.' }
    }
    if ($job.version -notmatch '^\d+\.\d+\.\d+$' -or [IO.Path]::GetFullPath($job.archive) -ne (Join-Path $cache 'package.zip')) { throw 'Invalid update job.' }
    $created = $false
    $mutex = [Threading.Mutex]::new($false, $(if($testOnly){'Local\OpenLnkuAcceptanceUpdate.Installer'}else{'Local\OpenLnku.Installer'}), [ref]$created)
    if (-not $created) { throw 'Another installer is running.' }
    if (Cancelled) { throw 'Update launch cancelled.' }
    if ($launchToken) {
        $readyPath = Join-Path $cache "ready-$launchToken.json"
        [IO.File]::WriteAllText($readyPath + '.new',(@{status='ready';launchToken=$launchToken}|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
        [IO.File]::Move($readyPath + '.new',$readyPath)
    }
    if ($parentProcess) {
        $exitDeadline = [DateTime]::UtcNow.AddSeconds(30)
        while (-not $parentProcess.WaitForExit(100)) {
            if (Cancelled) { throw 'Update launch cancelled.' }
            if ([DateTime]::UtcNow -ge $exitDeadline) { throw 'Client did not exit; update cancelled.' }
        }
    }
    if (Cancelled) { throw 'Update launch cancelled.' }
    $restart = -not $testOnly -or ($parentProcess -and $job.testRestart -eq $true)
    foreach ($kind in @('Live','Demo')) {
        $active = $null
        $identity = if ($testOnly) { 'OpenLnkuAcceptanceUpdate' } else { 'OpenLnku' }
        if ((-not $testOnly -or $parentProcess) -and [Threading.Mutex]::TryOpenExisting("Local\$identity.App.$kind",[ref]$active)) { $active.Dispose(); throw 'Client is still running.' }
    }
    if (Test-Path -LiteralPath $journalPath) {
        $journal = [IO.File]::ReadAllText($journalPath,[Text.Encoding]::UTF8) | ConvertFrom-Json
        if ($journal.state -eq 'applying' -and $journal.app -eq $app) {
            foreach ($name in $journal.old) { $oldFiles.Add((Relative $name)) | Out-Null }
            foreach ($name in $journal.changed) { $changed.Add((Relative $name)) }
            Rollback
            $changed.Clear(); [IO.File]::WriteAllText($journalPath,'{"state":"recovered"}')
            $status='success'; $message='已恢复上次更新前的版本'; return
        }
        throw 'Update job has already been used.'
    }
    AssertPlainPath $job.archive
    if ((Get-Item -LiteralPath $job.archive).Length -ne [long]$job.bytes -or (Hash $job.archive) -ne $job.sha256) { throw 'Archive integrity mismatch.' }
    if (Test-Path -LiteralPath $stage) { throw 'Unexpected staging directory.' }
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($job.archive)
    try {
        $prefix = "OpenLnku-$($job.version)-windows-x64/"
        $entries = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        [long]$expanded = 0
        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.StartsWith($prefix,[StringComparison]::Ordinal)) { throw 'Unexpected archive root.' }
            $name = $entry.FullName.Substring($prefix.Length)
            if (-not $name) { continue }
            if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400)) { throw 'Archive links are forbidden.' }
            if ($name.EndsWith('/')) { Relative $name.TrimEnd('/') | Out-Null; continue }
            $name = Relative $name
            if (-not $entries.Add($name) -or $entries.Count -gt 4096) { throw 'Duplicate or excessive package files.' }
            $expanded += $entry.Length
            if ($expanded -gt 536870912 -or $entry.Length -lt 0) { throw 'Expanded package exceeds limit.' }
            $target = Join-Path $stage $name; [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            $input = $entry.Open(); $output = [IO.File]::Open($target,[IO.FileMode]::CreateNew)
            try {
                $buffer = New-Object byte[] 65536; [long]$written=0
                while (($read=$input.Read($buffer,0,$buffer.Length)) -gt 0) { $written += $read; if ($written -gt $entry.Length) { throw 'ZIP entry size mismatch.' }; $output.Write($buffer,0,$read) }
                if ($written -ne $entry.Length) { throw 'Truncated ZIP entry.' }
            } finally { $output.Dispose(); $input.Dispose() }
        }
    } finally { $archive.Dispose() }
    $payloadPath = Join-Path $stage 'payload-manifest.json'
    $payload = [IO.File]::ReadAllText($payloadPath,[Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($payload.version -ne $job.version -or $payload.files.Count -gt 4095) { throw 'Package version mismatch.' }
    $newFiles = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in $payload.files) {
        $name = Relative $record.path
        if (-not $newFiles.Add($name) -or $name -eq 'payload-manifest.json' -or $record.sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid payload manifest.' }
        $path = Join-Path $stage $name
        if (-not $entries.Contains($name) -or (Get-Item -LiteralPath $path).Length -ne [long]$record.bytes -or (Hash $path) -ne $record.sha256) { throw 'Extracted package integrity mismatch.' }
    }
    $newFiles.Add('payload-manifest.json') | Out-Null
    if ($entries.Count -ne $newFiles.Count -or -not $newFiles.Contains('OpenLnku.exe') -or (Get-Item -LiteralPath (Join-Path $stage 'OpenLnku.exe')).VersionInfo.ProductVersion -ne $job.version) { throw 'Unexpected package content.' }
    $installed = Test-Path -LiteralPath (Join-Path $app '.opennex-installed')
    if ($installed) {
        if ((Get-Content -LiteralPath (Join-Path $app '.opennex-installed') -Raw) -ne 'OpenLnku') { throw 'Foreign installation marker.' }
        foreach ($name in [IO.File]::ReadAllLines((Join-Path $app '.opennex-files.txt'))) { if ($name -and -not $name.EndsWith('\')) { $oldFiles.Add((Relative $name.Replace('\','/'))) | Out-Null } }
    } else {
        $old = [IO.File]::ReadAllText((Join-Path $app 'payload-manifest.json'),[Text.Encoding]::UTF8) | ConvertFrom-Json
        foreach ($record in $old.files) { $oldFiles.Add((Relative $record.path)) | Out-Null }
        $oldFiles.Add('payload-manifest.json') | Out-Null
    }
    foreach ($name in $newFiles) {
        $target = Join-Path $app $name; AssertPlainPath $target
        if ([IO.File]::Exists($target) -and -not $oldFiles.Contains($name)) { throw 'Update would overwrite an unknown user file.' }
    }
    foreach ($name in $oldFiles) {
        $path = Join-Path $app $name; AssertPlainPath $path
        if (-not [IO.File]::Exists($path)) { throw 'Existing managed file missing; reinstall required.' }
        $copy = Join-Path $backup $name; [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy)) | Out-Null; [IO.File]::Copy($path,$copy,$false)
    }
    if ($installed) {
        $oldFiles.Add('.opennex-files.txt') | Out-Null
        [IO.File]::Copy((Join-Path $app '.opennex-files.txt'),(Join-Path $backup '.opennex-files.txt'),$false)
    }
    # A workspace can inherit a Low label. Normalize the new executable, restoring its DACL.
    $exe = Join-Path $stage 'OpenLnku.exe'; $acl = Get-Acl -LiteralPath $exe; $access = [Security.AccessControl.AccessControlSections]::Access
    $saved = $acl.GetSecurityDescriptorSddlForm($access); $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    if ($acl.GetOwner([Security.Principal.SecurityIdentifier]) -ne $sid) { throw 'Staged file owner mismatch.' }
    try { $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid,'TakeOwnership','Allow'))); Set-Acl -LiteralPath $exe -AclObject $acl; & "$env:WINDIR\System32\icacls.exe" $exe /setintegritylevel M | Out-Null; if ($LASTEXITCODE) { throw 'Unable to restore normal executable integrity.' } }
    finally { $restore=Get-Acl -LiteralPath $exe; $restore.SetSecurityDescriptorSddlForm($saved,$access); Set-Acl -LiteralPath $exe -AclObject $restore }
    $all = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $oldFiles) { $all.Add($name) | Out-Null }; foreach ($name in $newFiles) { $all.Add($name) | Out-Null }
    foreach ($name in @($all | Sort-Object @{Expression={if ($_ -eq 'OpenLnku.exe') {1} else {0}}}, @{Expression={$_}})) {
        if ($name -eq '.opennex-files.txt') { continue }
        $changed.Add($name); PersistJournal
        $target = Join-Path $app $name
        if ($newFiles.Contains($name)) {
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            AtomicCopy (Join-Path $stage $name) $target
        } else { [IO.File]::Delete($target) }
        if ($testOnly -and $job.failAfter -and $changed.Count -eq [int]$job.failAfter) { throw 'Injected acceptance failure.' }
    }
    # CopyFile retains an existing destination label; repeat executable normalization on the destination.
    $exe = Join-Path $app 'OpenLnku.exe'; $acl = Get-Acl -LiteralPath $exe; $saved = $acl.GetSecurityDescriptorSddlForm($access)
    try { $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid,'TakeOwnership','Allow'))); Set-Acl -LiteralPath $exe -AclObject $acl; & "$env:WINDIR\System32\icacls.exe" $exe /setintegritylevel M | Out-Null; if ($LASTEXITCODE) { throw 'Unable to restore destination integrity.' } }
    finally { $restore=Get-Acl -LiteralPath $exe; $restore.SetSecurityDescriptorSddlForm($saved,$access); Set-Acl -LiteralPath $exe -AclObject $restore }
    if ($installed) {
        $changed.Add('.opennex-files.txt'); PersistJournal
        [IO.File]::WriteAllLines((Join-Path $app '.opennex-files.txt'),[string[]]@($newFiles | Sort-Object | ForEach-Object {$_.Replace('/','\')}),[Text.UTF8Encoding]::new($false))
        $registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLnku'
        if (-not $testOnly -and (Test-Path -LiteralPath $registry) -and (Get-ItemPropertyValue -LiteralPath $registry -Name InstallLocation) -eq $app) { Set-ItemProperty -LiteralPath $registry -Name DisplayVersion -Value $job.version }
    }
    $status='success'; $message='更新完成'; [IO.File]::WriteAllText($journalPath,'{"state":"complete"}')
} catch {
    $message=$_.Exception.Message
    if ($changed.Count) { try { Rollback; $changed.Clear(); [IO.File]::WriteAllText($journalPath,'{"state":"recovered"}'); $message="更新失败，已恢复旧版：$message" } catch { $rollbackFailed=$true; $restart=$false; $message="自动恢复失败，请重新运行安装器。$($_.Exception.Message)" } }
    if (-not $rollbackFailed -and -not (Cancelled) -and -not $restart -and $parentProcess -and $parentProcess.HasExited) { $restart=$true }
} finally {
    if ($mutex) { $mutex.Dispose() }
    try { WriteOutcome } catch {}
    if ($restart) {
        # Hidden applies only to the PowerShell helper, never to the restarted GUI.
        try { Start-Process -FilePath (Join-Path $app 'OpenLnku.exe') -WorkingDirectory $app -ErrorAction Stop | Out-Null }
        catch { $status='restart-failed'; $message="更新处理已结束，但自动启动失败，请手动打开 OpenLnku。$($_.Exception.Message)"; try { WriteOutcome } catch {} }
    }
    if ($parentProcess) { $parentProcess.Dispose() }
}
if ($status -ne 'success') { exit 1 }
