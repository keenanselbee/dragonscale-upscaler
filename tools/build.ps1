param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [ValidateSet('debug', 'release', 'releasedbg')]
    [string]$Mode = 'releasedbg',

    [switch]$RefreshMod,

    [switch]$NoBootstrap,

    [switch]$CleanSdk
)

$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectRoot = Join-Path $repoRoot 'dev\projects\dragonscale-upscaler'
$sdkRoot = Join-Path $projectRoot 'external\FidelityFX-SDK-DX11\sdk'
$bootstrapScript = Join-Path $PSScriptRoot 'bootstrap-dev.ps1'
$fetchSdkScript = Join-Path $PSScriptRoot 'fetch-fidelityfx-sdk-dx11.ps1'
$buildSdkScript = Join-Path $PSScriptRoot 'build-fidelityfx-sdk-dx11.ps1'
$buildPluginScript = Join-Path $PSScriptRoot 'build-skse-plugin.ps1'
$buildOutputDir = Join-Path $repoRoot "tools\.cache\xmake-packages\build\windows\x64\$Mode"
$builtDll = Join-Path $buildOutputDir 'dragonscale-upscaler.dll'
$builtPdb = Join-Path $buildOutputDir 'dragonscale-upscaler.pdb'
$modDll = Join-Path $repoRoot 'mod\SKSE\Plugins\dragonscale-upscaler.dll'
$modPdb = Join-Path $repoRoot 'mod\SKSE\Plugins\dragonscale-upscaler.pdb'

function Invoke-CheckedScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [hashtable]$Parameters = @{}
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required script not found: $Path"
    }

    & $Path @Parameters
    if ($LASTEXITCODE -ne 0) {
        throw "$Path failed with exit code $LASTEXITCODE."
    }
}

if (-not $NoBootstrap) {
    Invoke-CheckedScript -Path $bootstrapScript
} else {
    Write-Host '[INFO] Skipping bootstrap because -NoBootstrap was provided.'
}

if (-not (Test-Path -LiteralPath $sdkRoot -PathType Container)) {
    Invoke-CheckedScript -Path $fetchSdkScript
}

$sdkParams = @{
    Configuration = $Configuration
}
if ($CleanSdk) {
    $sdkParams.Force = $true
}
Invoke-CheckedScript -Path $buildSdkScript -Parameters $sdkParams

$pluginParams = @{
    Mode = $Mode
}
if ($RefreshMod) {
    $pluginParams.RefreshMod = $true
}
Invoke-CheckedScript -Path $buildPluginScript -Parameters $pluginParams

foreach ($artifact in @($builtDll, $builtPdb)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Expected build artifact missing: $artifact"
    }
}

Write-Host "[SUCCESS] Built DLL: $builtDll"
Write-Host "[SUCCESS] Built PDB: $builtPdb"

if ($RefreshMod) {
    foreach ($artifact in @($modDll, $modPdb)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Expected mod artifact missing after refresh: $artifact"
        }
    }

    $hash = (Get-FileHash -LiteralPath $modDll -Algorithm SHA256).Hash
    Write-Host "[SUCCESS] Mod DLL: $modDll"
    Write-Host "[SUCCESS] Mod PDB: $modPdb"
    Write-Host "[SUCCESS] SHA256: $hash"
} else {
    $hash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    Write-Host "[SUCCESS] SHA256: $hash"
}
