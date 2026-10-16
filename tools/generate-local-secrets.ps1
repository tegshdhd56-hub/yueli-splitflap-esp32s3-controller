param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\firmware\Master_C3\LocalSecrets.h')
)

$ErrorActionPreference = 'Stop'

function Read-SecretText([string]$Prompt) {
    $secure = Read-Host $Prompt -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

$apPassword = Read-SecretText 'Enter a unique device access-point password (8-63 characters)'
if ($apPassword.Length -lt 8 -or $apPassword.Length -gt 63) {
    throw 'The access-point password must contain 8-63 characters.'
}

$adminPassword = Read-SecretText 'Enter a unique Developer page password'
if ($adminPassword.Length -lt 12) {
    throw 'Use a Developer page password with at least 12 characters.'
}

$escapedApPassword = $apPassword.Replace('\', '\\').Replace('"', '\"')
$digest = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($adminPassword))
$digestBytes = ($digest | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
$rows = @()
for ($i = 0; $i -lt 32; $i += 8) {
    $rows += '  ' + (($digestBytes -split ',')[$i..($i + 7)] -join ',') + $(if ($i -lt 24) { ', \' } else { '  \' })
}

$header = @"
#ifndef YUELI_LOCAL_SECRETS_H
#define YUELI_LOCAL_SECRETS_H

// Generated locally. Do not commit this file.
#define YUELI_SECRETS_CONFIGURED 1
#define MASTER_AP_PASS "$escapedApPassword"
#define YUELI_ADMIN_PASSWORD_SHA256_BYTES { \
$($rows -join "`r`n")
}

#endif
"@

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
[IO.File]::WriteAllText($resolvedOutput, $header, [Text.UTF8Encoding]::new($false))
Write-Host "Created $resolvedOutput"

$apPassword = $null
$adminPassword = $null
[Array]::Clear($digest, 0, $digest.Length)
