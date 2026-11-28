param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Compiler = (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    [string]$LegacyUcrt = (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\10.0.19041.0\ucrt\DLLs\x86'),
    [string]$HudGameDirectory,
    [string]$TextureArchive,
    [string]$XboxGlobal,
    [string]$XboxComplexDirectory,
    [string]$XboxAttribulator,
    [string]$XboxDotnet,
    [switch]$ConsoleUpdateOnly,
    [switch]$RuntimeOnly
)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
if ($RuntimeOnly -and $HudGameDirectory) {
    throw 'Use either -RuntimeOnly or -HudGameDirectory.'
}
if ($XboxAttribulator -and -not $XboxComplexDirectory) {
    throw 'Supply -XboxComplexDirectory when building the console attribute vault.'
}
if ($XboxDotnet -and -not $XboxAttribulator) {
    throw 'Supply -XboxAttribulator when using a portable x86 .NET host.'
}
if ($ConsoleUpdateOnly -and (-not $XboxComplexDirectory -or $RuntimeOnly -or $TextureArchive)) {
    throw 'The console update requires -XboxComplexDirectory and an existing Unlimited installation.'
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
    $materials = ''
    $console = ''
    $platformLights = ''
    $worldGlobal = ''
    if ($XboxComplexDirectory) {
        $console = (Resolve-Path -LiteralPath $XboxComplexDirectory).Path
        $gameData = if ($HudGameDirectory) { $HudGameDirectory } else { Join-Path $project 'Juego' }
        $materials = Join-Path $stage 'ConsoleMaterials.bin'
        $worldGlobal = Join-Path $gameData 'GLOBAL/GlobalB.lzc'
        & py.exe -3 (Join-Path $PSScriptRoot 'compile_console_materials.py') (Join-Path $console 'ModData/LightMaterials') (Join-Path $gameData 'GLOBAL/GlobalB.lzc') $materials
        if ($LASTEXITCODE) { throw "Console material compilation failed ($LASTEXITCODE)" }
        $platformLights = Join-Path $stage 'ConsolePlatformLights.bin'
        & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_platforms.py') $console (Join-Path $stage 'ConsolePlatforms') --lighting (Join-Path $gameData 'speed.exe') --lighting-output $platformLights
        if ($LASTEXITCODE) { throw "Console platform staging failed ($LASTEXITCODE)" }
        & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_reflections.py') (Join-Path $gameData 'GLOBAL/DYNTEX.BIN') (Join-Path $console 'ModData/Textures/DYNAMICTEXTURES') (Join-Path $stage 'DYNTEX36.BIN')
        if ($LASTEXITCODE) { throw "Console reflection staging failed ($LASTEXITCODE)" }
        & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_shader_textures.py') $gameData $console $stage
        if ($LASTEXITCODE) { throw "Console shader texture staging failed ($LASTEXITCODE)" }
        & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_renderer.py') $console (Join-Path $stage 'WorldRenderer')
        if ($LASTEXITCODE) { throw "Console world renderer staging failed ($LASTEXITCODE)" }
    }
    & (Join-Path $project 'build.ps1') -OutputFile (Join-Path $stage 'd3d9.dll') -XboxGlobal $XboxGlobal -XboxMaterials $materials -XboxBarriers $console -XboxPlatformLights $platformLights -XboxWorldGlobal $worldGlobal
    & (Join-Path $project 'third-party/discx-codec/build.ps1') -Destination $stage
    $setupDll = Join-Path $stage 'unlimited-setup-private.dll'
    & i686-w64-mingw32-clang.exe -std=c17 -O2 -Wall -Wextra -Werror -nostdlib -shared -fno-stack-protector '-Wl,--entry,_DllMain@12' (Join-Path $PSScriptRoot 'setup_private.c') (Join-Path $PSScriptRoot 'setup_private.def') -lkernel32 -lbcrypt -o $setupDll
    if ($LASTEXITCODE) { throw "Setup DLL compilation failed ($LASTEXITCODE)" }

    if (-not $ConsoleUpdateOnly -and -not (Test-Path -LiteralPath (Join-Path $LegacyUcrt 'ucrtbase.dll') -PathType Leaf)) { throw "Missing the Windows SDK x86 UCRT redistributable: $LegacyUcrt" }
    $defines = @("/DBuildPayload=$stage", "/DLegacyUcrt=$LegacyUcrt")
    $filename = 'NFSMW-Unlimited-Setup.exe'
    if ($ConsoleUpdateOnly) {
        $defines += '/DConsoleUpdateOnly'
        $filename = 'NFSMW-Unlimited-Console-Update.exe'
    } elseif ($RuntimeOnly) {
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
    if ($TextureArchive) {
        $textureSource = (Resolve-Path -LiteralPath $TextureArchive).Path
        & py.exe -3 (Join-Path $PSScriptRoot 'compile_textures.py') $textureSource (Join-Path $stage 'UnlimitedTextures.bin')
        if ($LASTEXITCODE) { throw "Texture compilation failed ($LASTEXITCODE)" }
        $defines += '/DEmbeddedTextures'
    }
    if ($XboxComplexDirectory) {
        if (-not $ConsoleUpdateOnly) {
            & py.exe -3 (Join-Path $PSScriptRoot 'compile_textures.py') $console (Join-Path $stage 'UnlimitedXboxTextures.bin') --xbox-complex $gameData
            if ($LASTEXITCODE) { throw "Console texture compilation failed ($LASTEXITCODE)" }
            & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_audio.py') $console $gameData $stage
            if ($LASTEXITCODE) { throw "Console audio staging failed ($LASTEXITCODE)" }
            Copy-Item -LiteralPath (Join-Path $console 'LICENSE') -Destination (Join-Path $stage 'Xbox360Stuff-LICENSE.txt')
            $defines += '/DEmbeddedConsolePack'
        }
        & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_track.py') (Join-Path $gameData 'TRACKS/STREAML2RA.BUN') (Join-Path $console 'Install-Part-3.ups') (Join-Path $stage 'STREAML2X6.BUN')
        if ($LASTEXITCODE) { throw "Console track staging failed ($LASTEXITCODE)" }
        $defines += '/DEmbeddedConsoleTrack'
        $defines += '/DEmbeddedConsolePlatforms'
        if ($XboxAttribulator) {
            $vaultArgs = @('--attribulator', $XboxAttribulator)
            if ($XboxDotnet) { $vaultArgs += @('--dotnet', $XboxDotnet) }
            & py.exe -3 (Join-Path $PSScriptRoot 'stage_console_vault.py') $console (Join-Path $gameData 'GLOBAL') (Join-Path $stage 'ATTRIBUTE360.BIN') --alternative-output (Join-Path $stage 'ATTRIBX36ALT.BIN') @vaultArgs
            if ($LASTEXITCODE) { throw "Console vault staging failed ($LASTEXITCODE)" }
            $defines += '/DEmbeddedConsoleVault'
        }
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
