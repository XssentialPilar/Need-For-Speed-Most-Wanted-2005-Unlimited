param([string]$OutputFile = (Join-Path $PSScriptRoot 'd3d9.dll'))
$ErrorActionPreference = 'Stop'
$toolDir = Split-Path (Get-Command i686-w64-mingw32-windres.exe).Source
$resource = Join-Path ([IO.Path]::GetTempPath()) ("unlimited-" + [guid]::NewGuid() + ".o")
try {
    & (Join-Path $toolDir 'i686-w64-mingw32-windres.exe') -i (Join-Path $PSScriptRoot 'Unlimited.rc') -O coff -o $resource
    if ($LASTEXITCODE) { throw "Resource compilation failed ($LASTEXITCODE)" }
    $vkInclude = Join-Path $env:VULKAN_SDK 'Include'
    & (Join-Path $toolDir 'i686-w64-mingw32-clang++.exe') -std=c++17 -Wall -Wextra -Werror -O2 -static -shared -idirafter $vkInclude (Join-Path $PSScriptRoot 'vram_probe.cpp') (Join-Path $PSScriptRoot 'd3d9.def') $resource -o $OutputFile
    if ($LASTEXITCODE) { throw "DLL compilation failed ($LASTEXITCODE)" }
} finally {
    if (Test-Path -LiteralPath $resource) { Remove-Item -LiteralPath $resource }
}
