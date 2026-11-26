param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Compiler = (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    [string]$LegacyUcrt = (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\10.0.19041.0\ucrt\DLLs\x86')
)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
$stage = Join-Path ([IO.Path]::GetTempPath()) ('unlimited-installer-' + [Guid]::NewGuid().ToString('N'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($stage)
[void][IO.Directory]::CreateDirectory($output)

& (Join-Path $project 'build.ps1') -OutputFile (Join-Path $stage 'd3d9.dll')
$setupDll = Join-Path $stage 'unlimited-setup-private.dll'
& i686-w64-mingw32-clang.exe -std=c17 -O2 -Wall -Wextra -Werror -nostdlib -shared -fno-stack-protector '-Wl,--entry,_DllMain@12' (Join-Path $PSScriptRoot 'setup_private.c') (Join-Path $PSScriptRoot 'setup_private.def') -lkernel32 -lbcrypt -o $setupDll
if ($LASTEXITCODE) { throw "Setup DLL compilation failed ($LASTEXITCODE)" }

if (-not (Test-Path -LiteralPath (Join-Path $LegacyUcrt 'ucrtbase.dll') -PathType Leaf)) { throw "Missing the Windows SDK x86 UCRT redistributable: $LegacyUcrt" }
& $Compiler "/DBuildPayload=$stage" "/DLegacyUcrt=$LegacyUcrt" "/O$output" (Join-Path $PSScriptRoot 'Unlimited.iss')
if ($LASTEXITCODE) { throw "Inno Setup compilation failed ($LASTEXITCODE)" }
Write-Output (Join-Path $output 'NFSMW-Unlimited-Setup.exe')
