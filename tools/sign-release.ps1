#requires -Version 7.4
param([switch]$InitializeKey)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((& (Join-Path $PSScriptRoot 'inspect-process-integrity.ps1')).integrityRID -lt 8192) { throw 'Release signing requires your normal user session.' }
$privateDir = Join-Path $taskRoot '.private'
$keyPath = Join-Path $privateDir 'release-key.dpapi'
$publicPath = Join-Path $taskRoot 'packaging/update-public-key.hex'
Add-Type -AssemblyName System.Security
$ecdsa = [Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256'))
try {
    if ($InitializeKey) {
        if (Test-Path -LiteralPath $keyPath) { throw 'An update key already exists. Refusing rotation.' }
        New-Item -ItemType Directory -Path $privateDir -Force | Out-Null
        $privateBytes = $ecdsa.ExportPkcs8PrivateKey()
        try {
            $protected = [Security.Cryptography.ProtectedData]::Protect($privateBytes, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
            [IO.File]::WriteAllBytes($keyPath, $protected)
            [IO.File]::WriteAllBytes((Join-Path $privateDir 'release-key-backup.dpapi'), $protected)
        } finally { [Array]::Clear($privateBytes) }
        $parameters = $ecdsa.ExportParameters($false)
        # BCRYPT_ECDSA_PUBLIC_P256_MAGIC, cbKey=32, X and Y in big-endian order.
        $publicBlob = [byte[]](@(0x45,0x43,0x53,0x31,0x20,0,0,0) + $parameters.Q.X + $parameters.Q.Y)
        [IO.File]::WriteAllText($publicPath, [Convert]::ToHexString($publicBlob), [Text.UTF8Encoding]::new($false))
        Write-Output 'Update public key generated. Private key is DPAPI protected and excluded from releases.'
        return
    }
    $protected = [IO.File]::ReadAllBytes($keyPath)
    $privateBytes = [Security.Cryptography.ProtectedData]::Unprotect($protected, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
    try { $read = 0; $ecdsa.ImportPkcs8PrivateKey($privateBytes, [ref]$read) } finally { [Array]::Clear($privateBytes) }
    $parameters = $ecdsa.ExportParameters($false)
    $publicBlob = [byte[]](@(0x45,0x43,0x53,0x31,0x20,0,0,0) + $parameters.Q.X + $parameters.Q.Y)
    if ([Convert]::ToHexString($publicBlob) -ne (Get-Content -LiteralPath $publicPath -Raw).Trim()) { throw 'Private and embedded public key do not match.' }
    $version = (Get-Content (Join-Path $taskRoot 'VERSION.txt') -Raw).Trim()
    $archive = Get-Item -LiteralPath (Join-Path $taskRoot "dist/OpenLnku-$version-windows-x64.zip")
    $manifest = [ordered]@{schema=1;version=$version;platform='windows-x64';archive=$archive.Name;bytes=$archive.Length;sha256=(Get-FileHash -LiteralPath $archive.FullName).Hash.ToLowerInvariant()}
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Compress))
    $signature = $ecdsa.SignData($bytes, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    if (-not $ecdsa.VerifyData($bytes,$signature,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)) { throw 'Signature self-check failed.' }
    [IO.File]::WriteAllBytes((Join-Path $taskRoot 'dist/update.json'),$bytes)
    [IO.File]::WriteAllBytes((Join-Path $taskRoot 'dist/update.sig'),$signature)
    Write-Output 'Signed update.json and update.sig generated; exact manifest bytes verified.'
} finally { if ($ecdsa) { $ecdsa.Dispose() } }
