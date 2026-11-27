param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$Cache = (Join-Path ([IO.Path]::GetTempPath()) 'nfs-discx-build')
)

$ErrorActionPreference = 'Stop'
$ffmpegVersion = '6.1.6'
$sourceName = "ffmpeg-$ffmpegVersion.tar.xz"
$archive = Join-Path $Cache $sourceName
$root = Join-Path $Cache "ffmpeg-$ffmpegVersion"
$recipe = Join-Path $PSScriptRoot 'build-ffmpeg.sh'
$recipeHash = (Get-FileHash -LiteralPath $recipe).Hash
$stamp = Join-Path $Cache 'ffmpeg-recipe.sha256'
$libraryNames = @('libavformat', 'libavcodec', 'libswresample', 'libavutil')

function Get-VerifiedArchive($Location, $Url, $Sha256) {
    if (!(Test-Path -LiteralPath $Location)) {
        $partial = "$Location.part"
        & curl.exe -fLsS $Url -o $partial
        if ($LASTEXITCODE -or (Get-FileHash -LiteralPath $partial).Hash -ne $Sha256) {
            throw "Download verification failed for $Url"
        }
        Move-Item -LiteralPath $partial -Destination $Location
    }
    if ((Get-FileHash -LiteralPath $Location).Hash -ne $Sha256) {
        throw "Checksum mismatch for $Location"
    }
}

function ConvertTo-MsysPath([string]$Path) {
    $posix = $Path.Replace('\', '/')
    return '/' + $posix.Substring(0, 1).ToLowerInvariant() + $posix.Substring(2)
}

[void][IO.Directory]::CreateDirectory($Cache)
Get-VerifiedArchive $archive "https://ffmpeg.org/releases/$sourceName" 'D4FCB164028DD3BEEE5D92C0AC72E46AAC6973C75EA12DC14DE07BF8F407370A'
$dllNames = @('avformat-60.dll', 'avcodec-60.dll', 'swresample-4.dll', 'avutil-58.dll')
$missing = for ($i = 0; $i -lt $libraryNames.Count; ++$i) {
    if (!(Test-Path -LiteralPath (Join-Path $root "$($libraryNames[$i])/$($dllNames[$i])"))) { $dllNames[$i] }
}
if ($missing -or !(Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -ne $recipeHash) {
    if (!(Test-Path -LiteralPath (Join-Path $root 'configure'))) {
        & tar.exe -xf $archive -C $Cache
        if ($LASTEXITCODE) { throw 'FFmpeg source extraction failed' }
    }
    $gitRoot = Split-Path (Split-Path (Get-Command git.exe).Source)
    $bash = Join-Path $gitRoot 'usr/bin/bash.exe'
    if (!(Test-Path -LiteralPath $bash)) { throw 'Git for Windows with Bash is required to build the audio decoder' }

    $make = Join-Path $Cache 'usr/bin/make.exe'
    if (!(Test-Path -LiteralPath $make)) {
        $makeArchive = Join-Path $Cache 'make.tar.zst'
        Get-VerifiedArchive $makeArchive 'https://mirror.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst' 'AF0BDBA17F06FE037F0194069ADAA31A8FE45F1A11381501896AEA1FAE37BD5D'
        & tar.exe -xf $makeArchive -C $Cache 'usr/bin/make.exe'
        if ($LASTEXITCODE) { throw 'GNU Make extraction failed' }
    }
    Copy-Item -LiteralPath $recipe -Destination (Join-Path $Cache 'build.sh') -Force
    $env:DISCX_BUILD_POSIX = ConvertTo-MsysPath ([IO.Path]::GetFullPath($Cache))
    $env:DISCX_LLVM_POSIX = ConvertTo-MsysPath (Split-Path (Get-Command i686-w64-mingw32-clang.exe).Source)
    $env:DISCX_FFMPEG_VERSION = $ffmpegVersion
    $log = Join-Path $Cache 'ffmpeg.log'
    & $bash --noprofile --norc -c 'root="$DISCX_BUILD_POSIX"; export PATH="$root/usr/bin:$DISCX_LLVM_POSIX:/usr/bin:/mingw64/bin:$PATH"; cd "$root/ffmpeg-$DISCX_FFMPEG_VERSION" && bash "$root/build.sh" > "$root/ffmpeg.log" 2>&1'
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 15
        throw "FFmpeg build failed. See $log"
    }
    Set-Content -LiteralPath $stamp -Value $recipeHash
}

$dir = Join-Path $Destination 'Discx'
$sourceDir = Join-Path $dir 'Source'
[void][IO.Directory]::CreateDirectory($sourceDir)
$libs = for ($i = 0; $i -lt $libraryNames.Count; ++$i) {
    $lib = $libraryNames[$i]
    Copy-Item -LiteralPath (Join-Path $root "$lib/$($dllNames[$i])") -Destination $dir -Force
    Join-Path $root "$lib/$lib.dll.a"
}
& i686-w64-mingw32-clang++.exe -std=c++17 -Wall -Wextra -Werror -O2 -shared -static -idirafter $root (Join-Path $PSScriptRoot 'codec.cpp') @libs -o (Join-Path $dir 'UnlimitedDiscx.dll')
if ($LASTEXITCODE) { throw "Discx decoder compilation failed ($LASTEXITCODE)" }
Copy-Item -LiteralPath $archive, $recipe -Destination $sourceDir -Force
Copy-Item -LiteralPath (Join-Path $root 'COPYING.LGPLv2.1') -Destination (Join-Path $dir 'FFmpeg-LGPL-2.1.txt') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $dir 'FFmpeg-Notice.md') -Force
