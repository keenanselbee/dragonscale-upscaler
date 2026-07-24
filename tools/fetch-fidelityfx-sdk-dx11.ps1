param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$projectRoot = Join-Path $repoRoot 'dev\projects\dragonscale-upscaler'
$externalRoot = Join-Path $projectRoot 'external'
$targetRoot = Join-Path $externalRoot 'FidelityFX-SDK-DX11'
$cacheRoot = Join-Path $repoRoot 'tools\.cache'
$archivePath = Join-Path $cacheRoot 'FidelityFX-SDK-DX11-optiscaler-build.zip'
$url = 'https://github.com/alandtse/FidelityFX-SDK-DX11/archive/refs/heads/optiscaler-build.zip'

if (Test-Path $targetRoot) {
    if (-not $Force) {
        Write-Host "FidelityFX SDK DX11 source already exists: $targetRoot"
        Write-Host "Use -Force to replace it."
        exit 0
    }

    Remove-Item -LiteralPath $targetRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $externalRoot | Out-Null
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null

if (-not (Test-Path $archivePath)) {
    Write-Host "Downloading FidelityFX SDK DX11 source..."
    Write-Host $url
    Invoke-WebRequest -Uri $url -OutFile $archivePath -Headers @{ 'User-Agent' = 'DragonScale-FidelityFX-Fetch/1.0' }
} else {
    Write-Host "Using cached archive: $archivePath"
}

$extractTemp = Join-Path $cacheRoot ('FidelityFX-SDK-DX11-extract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $extractTemp | Out-Null

try {
    Write-Host "Extracting SDK..."
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractTemp -Force

    $root = Get-ChildItem -LiteralPath $extractTemp -Directory | Select-Object -First 1
    if (-not $root) {
        throw 'Could not find extracted SDK root directory.'
    }

    Move-Item -LiteralPath $root.FullName -Destination $targetRoot
    Write-Host "Installed FidelityFX SDK DX11 source:"
    Write-Host "  $targetRoot"
} finally {
    if (Test-Path $extractTemp) {
        Remove-Item -LiteralPath $extractTemp -Recurse -Force
    }
}
