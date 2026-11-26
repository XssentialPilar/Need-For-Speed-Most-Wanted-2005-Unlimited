param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Compiler = (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    [string]$LegacyUcrt = (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\10.0.19041.0\ucrt\DLLs\x86'),
    [string]$HudGameDirectory,
    [switch]$RuntimeOnly
)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
if ($RuntimeOnly -and $HudGameDirectory) {
    throw 'Use either -RuntimeOnly or -HudGameDirectory.'
}
if (-not $RuntimeOnly -and -not $HudGameDirectory) {
    $HudGameDirectory = Join-Path $project 'Juego'
}
if (-not $RuntimeOnly -and -not (Test-Path -LiteralPath $HudGameDirectory -PathType Container)) {
    throw 'The complete installer requires a verified HD game folder. Set -HudGameDirectory to that folder.'
}

$scratchRoot = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path
$stage = Join-Path $scratchRoot ('unlimited-installer-' + [Guid]::NewGuid().ToString('N'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($stage)
[void][IO.Directory]::CreateDirectory($output)

try {
    & (Join-Path $project 'build.ps1') -OutputFile (Join-Path $stage 'd3d9.dll')
    $setupDll = Join-Path $stage 'unlimited-setup-private.dll'
    & i686-w64-mingw32-clang.exe -std=c17 -O2 -Wall -Wextra -Werror -nostdlib -shared -fno-stack-protector '-Wl,--entry,_DllMain@12' (Join-Path $PSScriptRoot 'setup_private.c') (Join-Path $PSScriptRoot 'setup_private.def') -lkernel32 -lbcrypt -o $setupDll
    if ($LASTEXITCODE) { throw "Setup DLL compilation failed ($LASTEXITCODE)" }

    if (-not (Test-Path -LiteralPath (Join-Path $LegacyUcrt 'ucrtbase.dll') -PathType Leaf)) { throw "Missing the Windows SDK x86 UCRT redistributable: $LegacyUcrt" }
    $defines = @("/DBuildPayload=$stage", "/DLegacyUcrt=$LegacyUcrt")
    $filename = 'NFSMW-Unlimited-Setup.exe'
    if ($RuntimeOnly) {
        $defines += '/DRuntimeOnlyBuild'
        $filename = 'NFSMW-Unlimited-Runtime-Setup.exe'
    } else {
        $sourceRoot = (Resolve-Path -LiteralPath $HudGameDirectory).Path
        $manifestPath = Join-Path $PSScriptRoot 'upscaled-hud-checksums.tsv'
        $manifest = Import-Csv -LiteralPath $manifestPath -Delimiter "`t" -Header Relative, Original, HD
        $staged = [System.Collections.Generic.HashSet[string]]::new()
        $entries = foreach ($asset in $manifest) {
            $source = Join-Path $sourceRoot $asset.Relative
            if ((Get-FileHash -LiteralPath $source).Hash -ne $asset.HD) {
                throw "Unsupported HD asset: $($asset.Relative)"
            }
            if ($staged.Add($asset.HD)) {
                $target = Join-Path $stage ($asset.HD + '.hdasset')
                Copy-Item -LiteralPath $source -Destination $target
                if ((Get-FileHash -LiteralPath $target).Hash -ne $asset.HD) {
                    throw "Could not stage HD asset: $($asset.Relative)"
                }
                'Source: "{0}"; Flags: dontcopy' -f $target
            }
        }
        if (-not $staged.Count) { throw 'The HD payload is empty.' }
        $include = Join-Path $stage 'EmbeddedHudPayload.iss'
        [IO.File]::WriteAllLines($include, [string[]]$entries, [Text.UTF8Encoding]::new($true))
        $defines += "/DEmbeddedHudPayload=$include"
    }
    & $Compiler @defines "/O$output" (Join-Path $PSScriptRoot 'Unlimited.iss')
    if ($LASTEXITCODE) { throw "Inno Setup compilation failed ($LASTEXITCODE)" }
    Write-Output (Join-Path $output $filename)
} finally {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path -Parent $resolvedStage) -eq $scratchRoot.TrimEnd('\') -and
        (Split-Path -Leaf $resolvedStage) -match '^unlimited-installer-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedStage -Recurse
    }
}
