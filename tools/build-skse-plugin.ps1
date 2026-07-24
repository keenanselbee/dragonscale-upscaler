param(
    [ValidateSet('debug', 'release', 'releasedbg')]
    [string]$Mode = 'releasedbg',

    [switch]$RefreshMod,

    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

if ($VerifyOnly -and $RefreshMod) {
    throw 'Choose either -VerifyOnly or -RefreshMod, not both.'
}

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repoPluginDir = Join-Path $repo 'mod\SKSE\Plugins'
$repoDll = Join-Path $repoPluginDir 'dragonscale-upscaler.dll'
$repoPdb = Join-Path $repoPluginDir 'dragonscale-upscaler.pdb'
$pluginProject = Join-Path $repo 'dev\projects\dragonscale-upscaler'
$pluginSource = Join-Path $pluginProject 'src'
$xmakeLua = Join-Path $pluginProject 'xmake.lua'
$pluginHeader = Join-Path $pluginSource 'plugin.h'
$targetName = 'dragonscale-upscaler'
$xmake = Join-Path $repo 'dev\tools\xmake\xmake.exe'
$xmakeGlobalDir = Join-Path $repo 'dev\.xmake'
$xmakePackageCache = Join-Path $repo 'tools\.cache\xmake-packages'
$builtOutputDir = Join-Path $xmakePackageCache "build\windows\x64\$Mode"
$builtDll = Join-Path $builtOutputDir 'dragonscale-upscaler.dll'
$builtPdb = Join-Path $builtOutputDir 'dragonscale-upscaler.pdb'
$sevenZipRoot = Join-Path $repo 'dev\tools\7z'
$sevenZipExe = Join-Path $sevenZipRoot '7z.exe'

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-PathInside([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label path is outside expected root: $fullPath"
    }
}

function Assert-FileExists([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Assert-DirectoryExists([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label not found: $Path"
    }
}

function Get-ShortPath([string]$Path) {
    $command = 'for %I in ("' + $Path + '") do @echo %~sI'
    $shortPath = & cmd.exe /d /c $command
    if ($LASTEXITCODE -eq 0 -and $shortPath) {
        return ($shortPath | Select-Object -First 1)
    }

    return $Path
}

function Patch-XmakeBundleUnzip([string]$PackageCache) {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) '.xmake0'
    if (-not (Test-Path -LiteralPath $tempRoot -PathType Container)) {
        return
    }

    $unzipScripts = Get-ChildItem -LiteralPath $tempRoot -Recurse -Filter 'unzip.ps1' -ErrorAction SilentlyContinue
    foreach ($script in $unzipScripts) {
        $text = Get-Content -LiteralPath $script.FullName -Raw
        if ($text.Contains('DRAGONSCALE_XMAKE_PACKAGE_CACHE')) {
            continue
        }

        $normalizedText = $text -replace "`r`n", "`n"
        $old = @'
    function unzip {
        try {
            Expand-Archive -Path $archivefile -DestinationPath $outputdir
        } catch {
'@
        $new = @'
    function resolveArchiveFile {
        if (Test-Path -LiteralPath $archivefile -PathType Leaf) {
            return $archivefile
        }

        if ($env:DRAGONSCALE_XMAKE_PACKAGE_CACHE) {
            foreach ($cacheRoot in ($env:DRAGONSCALE_XMAKE_PACKAGE_CACHE -split ';')) {
                if (-not $cacheRoot) {
                    continue
                }

                $candidate = Join-Path $cacheRoot $archivefile
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    return $candidate
                }
            }
        }

        return $archivefile
    }

    function unzip {
        try {
            $resolvedArchiveFile = resolveArchiveFile
            Expand-Archive -LiteralPath $resolvedArchiveFile -DestinationPath $outputdir
        } catch {
'@

        $normalizedOld = $old -replace "`r`n", "`n"
        $normalizedNew = $new -replace "`r`n", "`n"
        if (-not $normalizedText.Contains($normalizedOld)) {
            throw "Could not patch xmake unzip helper: $($script.FullName)"
        }

        $patchedText = $normalizedText.Replace($normalizedOld, $normalizedNew) -replace "`n", "`r`n"
        Set-Content -LiteralPath $script.FullName -Value $patchedText -NoNewline
    }
}

if ((Get-FullPath (Get-Location).Path) -ne (Get-FullPath $repo)) {
    Set-Location -LiteralPath $repo
}

Assert-DirectoryExists $repo 'repo root'
Assert-DirectoryExists $pluginProject 'plugin project'
Assert-DirectoryExists $pluginSource 'plugin source'
Assert-FileExists $xmakeLua 'xmake.lua'
Assert-FileExists $pluginHeader 'plugin header'
Assert-FileExists $xmake 'repo-local xmake'
Assert-PathInside $pluginProject $repo 'plugin project'
Assert-PathInside $pluginSource $pluginProject 'plugin source'
Assert-PathInside $builtDll $xmakePackageCache 'built DLL'
Assert-PathInside $builtPdb $xmakePackageCache 'built PDB'
Assert-PathInside $repoDll $repo 'repo DLL'
Assert-PathInside $repoPdb $repo 'repo PDB'
Assert-PathInside $xmakeLua $pluginProject 'xmake.lua'
Assert-PathInside $pluginHeader $pluginProject 'plugin header'
Assert-PathInside $xmake $repo 'xmake'
Assert-PathInside $xmakeGlobalDir $repo 'xmake global dir'
Assert-PathInside $xmakePackageCache $repo 'xmake package cache'
Assert-PathInside $sevenZipRoot $repo '7-Zip root'
Assert-FileExists $sevenZipExe 'repo-local 7-Zip'

if (-not (Test-Path -LiteralPath $xmakePackageCache -PathType Container)) {
    New-Item -ItemType Directory -Path $xmakePackageCache -Force | Out-Null
}
$xmakePackageCacheForXmake = Get-ShortPath $xmakePackageCache

$env:XMAKE_GLOBALDIR = $xmakeGlobalDir
$env:DRAGONSCALE_XMAKE_PACKAGE_CACHE = $xmakePackageCacheForXmake
$env:PATH = "$sevenZipRoot;$env:PATH"
Set-Location -LiteralPath $xmakePackageCache

$copyToMod = 'n'

Write-Host "[INFO] Using xmake: $xmake"
Write-Host "[INFO] Using 7-Zip: $sevenZipExe"
Write-Host "[INFO] XMAKE_GLOBALDIR: $xmakeGlobalDir"
Write-Host "[INFO] xmake package cache: $xmakePackageCacheForXmake"
& $sevenZipExe --help | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip version check failed with exit code $LASTEXITCODE."
}
& $xmake --version | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "xmake version check failed with exit code $LASTEXITCODE."
}
Patch-XmakeBundleUnzip $xmakePackageCacheForXmake
& $xmake g --pkg_searchdirs=$xmakePackageCacheForXmake
if ($LASTEXITCODE -ne 0) {
    throw "xmake global package cache configuration failed with exit code $LASTEXITCODE."
}

Write-Host "[INFO] Configuring $Mode build..."
& $xmake f -P $pluginProject -y -m $Mode --toolchain=msvc --skyrim_se=y --skyrim_ae=y --copy_to_mod=$copyToMod --pkg_searchdirs=$xmakePackageCacheForXmake
if ($LASTEXITCODE -ne 0) {
    throw "xmake configure failed with exit code $LASTEXITCODE."
}

Write-Host '[INFO] Building DragonScale SKSE plugin...'
& $xmake build -P $pluginProject $targetName
if ($LASTEXITCODE -ne 0) {
    throw "xmake build failed with exit code $LASTEXITCODE."
}

Assert-FileExists $builtDll 'built DLL'
Assert-FileExists $builtPdb 'built PDB'

$builtDllInfo = Get-Item -LiteralPath $builtDll
$builtPdbInfo = Get-Item -LiteralPath $builtPdb
if ($builtDllInfo.Length -le 0) {
    throw "Built DLL is empty: $builtDll"
}
if ($builtPdbInfo.Length -le 0) {
    throw "Built PDB is empty: $builtPdb"
}

if ($RefreshMod) {
    if (-not (Test-Path -LiteralPath $repoPluginDir -PathType Container)) {
        New-Item -ItemType Directory -Path $repoPluginDir -Force | Out-Null
    }

    Copy-Item -LiteralPath $builtDll -Destination $repoDll -Force
    Copy-Item -LiteralPath $builtPdb -Destination $repoPdb -Force

    $builtHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    $repoHash = (Get-FileHash -LiteralPath $repoDll -Algorithm SHA256).Hash
    if ($builtHash -ne $repoHash) {
        throw 'Repo DLL hash does not match built DLL after copy.'
    }

    Assert-FileExists $repoPdb 'repo PDB'
    Write-Host "[SUCCESS] Mod DLL refreshed: $repoDll"
    Write-Host "[SUCCESS] Mod PDB refreshed: $repoPdb"
    Write-Host "[SUCCESS] SHA256: $repoHash"
} else {
    $builtHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    Write-Host "[SUCCESS] Build completed without refreshing mod files."
    Write-Host "[SUCCESS] Built DLL: $builtDll"
    Write-Host "[SUCCESS] Built PDB: $builtPdb"
    Write-Host "[SUCCESS] SHA256: $builtHash"
}
