param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [int]$Parallel = 8,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$projectRoot = Join-Path $repoRoot 'dev\projects\dragonscale-upscaler'
$sdkRoot = Join-Path $projectRoot 'external\FidelityFX-SDK-DX11\sdk'
$buildRoot = Join-Path $sdkRoot 'build'
$libRoot = Join-Path $sdkRoot 'bin\ffx_sdk'

function Set-PatchedText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $current = Get-Content -LiteralPath $Path -Raw
    if ($current -ne $Text) {
        Set-Content -LiteralPath $Path -Value $Text -NoNewline
    }
}

function Replace-RequiredText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,

        [Parameter(Mandatory = $true)]
        [string]$Old,

        [Parameter(Mandatory = $true)]
        [string]$New,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $normalizedText = $Text -replace "`r`n", "`n"
    $normalizedOld = $Old -replace "`r`n", "`n"
    $normalizedNew = $New -replace "`r`n", "`n"

    if ($normalizedText.Contains($normalizedNew)) {
        return $Text
    }

    if (-not $normalizedText.Contains($normalizedOld)) {
        throw "Could not patch $Path because the expected source text was not found."
    }

    return ($normalizedText.Replace($normalizedOld, $normalizedNew) -replace "`n", "`r`n")
}

function Patch-FidelityFxDx11Source {
    $shaderBlobsPath = Join-Path $sdkRoot 'src\backends\shared\ffx_shader_blobs.cpp'
    $cmakeListsPath = Join-Path $sdkRoot 'src\backends\dx11\CMakeLists.txt'
    $dx11BackendPath = Join-Path $sdkRoot 'src\backends\dx11\ffx_dx11.cpp'

    foreach ($path in @($shaderBlobsPath, $cmakeListsPath, $dx11BackendPath)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required FidelityFX SDK source file is missing: $path"
        }
    }

    $shaderBlobs = Get-Content -LiteralPath $shaderBlobsPath -Raw
    $shaderBlobs = Replace-RequiredText `
        -Text $shaderBlobs `
        -Path $shaderBlobsPath `
        -Old "#if defined(FFX_FSR) || defined(FFX_ALL)`r`n#include `"blob_accessors/ffx_fsr2_shaderblobs.h`"`r`n#endif // #if defined(FFX_FSR) || defined(FFX_ALL)" `
        -New "#if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)`r`n#include `"blob_accessors/ffx_fsr2_shaderblobs.h`"`r`n#endif // #if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)"
    $shaderBlobs = Replace-RequiredText `
        -Text $shaderBlobs `
        -Path $shaderBlobsPath `
        -Old "#if defined(FFX_FSR) || defined(FFX_ALL)`r`n    case FFX_EFFECT_FSR2:`r`n        return fsr2GetPermutationBlobByIndex((FfxFsr2Pass)passId, permutationOptions, outBlob);`r`n#endif // #if defined(FFX_FSR) || defined(FFX_ALL)" `
        -New "#if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)`r`n    case FFX_EFFECT_FSR2:`r`n        return fsr2GetPermutationBlobByIndex((FfxFsr2Pass)passId, permutationOptions, outBlob);`r`n#endif // #if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)"
    $shaderBlobs = Replace-RequiredText `
        -Text $shaderBlobs `
        -Path $shaderBlobsPath `
        -Old "#if defined(FFX_FSR) || defined(FFX_ALL)`r`n    case FFX_EFFECT_FSR2:`r`n        return fsr2IsWave64(permutationOptions, isWave64);`r`n#endif // #if defined(FFX_FSR) || defined(FFX_ALL)" `
        -New "#if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)`r`n    case FFX_EFFECT_FSR2:`r`n        return fsr2IsWave64(permutationOptions, isWave64);`r`n#endif // #if defined(FFX_FSR2) || defined(FFX_FSR) || defined(FFX_ALL)"
    Set-PatchedText -Path $shaderBlobsPath -Text $shaderBlobs

    $cmakeLists = Get-Content -LiteralPath $cmakeListsPath -Raw
    $fsr2SourceBlock = @'
if (FFX_FSR2 OR FFX_FSR OR FFX_ALL)
	set(FFX_FSR2_PRIVATE_SOURCE
		"${FFX_SRC_BACKENDS_PATH}/shared/blob_accessors/ffx_fsr2_shaderblobs.h"
		"${FFX_SRC_BACKENDS_PATH}/shared/blob_accessors/ffx_fsr2_shaderblobs.cpp")
	list(APPEND PRIVATE_SOURCE ${FFX_FSR2_PRIVATE_SOURCE})
endif()

'@
    if (-not $cmakeLists.Contains($fsr2SourceBlock)) {
        $cmakeLists = Replace-RequiredText `
            -Text $cmakeLists `
            -Path $cmakeListsPath `
            -Old "if (FFX_FSR OR FFX_ALL)`r`n`tset(FFX_FSR_PRIVATE_SOURCE" `
            -New ($fsr2SourceBlock + "if (FFX_FSR OR FFX_ALL)`r`n`tset(FFX_FSR_PRIVATE_SOURCE")
    }

    $fsr2ShaderBlock = @'
if (FFX_FSR2 OR FFX_FSR OR FFX_ALL)
	target_compile_definitions(ffx_backend_dx11_${FFX_PLATFORM_NAME} PRIVATE FFX_FSR2)
	include (CMakeShadersFSR2.txt)
endif()

'@
    if (-not $cmakeLists.Contains($fsr2ShaderBlock)) {
        $cmakeLists = Replace-RequiredText `
            -Text $cmakeLists `
            -Path $cmakeListsPath `
            -Old "if (FFX_FSR OR FFX_ALL)`r`n`ttarget_compile_definitions(ffx_backend_dx11_`${FFX_PLATFORM_NAME} PRIVATE FFX_FSR)" `
            -New ($fsr2ShaderBlock + "if (FFX_FSR OR FFX_ALL)`r`n`ttarget_compile_definitions(ffx_backend_dx11_`${FFX_PLATFORM_NAME} PRIVATE FFX_FSR)")
    }
    Set-PatchedText -Path $cmakeListsPath -Text $cmakeLists

    $dx11Backend = Get-Content -LiteralPath $dx11BackendPath -Raw
    $diagnosticGlobals = @'
static HRESULT g_lastCreateComputeShaderResultDX11 = S_OK;

extern "C" HRESULT ffxGetLastCreateComputeShaderHRESULTDX11()
{
    return g_lastCreateComputeShaderResultDX11;
}

'@
    if (-not $dx11Backend.Contains('ffxGetLastCreateComputeShaderHRESULTDX11')) {
        $dx11Backend = Replace-RequiredText `
            -Text $dx11Backend `
            -Path $dx11BackendPath `
            -Old "// fix up format in case resource passed for UAV cannot be mapped" `
            -New ($diagnosticGlobals + "// fix up format in case resource passed for UAV cannot be mapped")
    }

    $dx11Backend = Replace-RequiredText `
        -Text $dx11Backend `
        -Path $dx11BackendPath `
        -Old "        const HRESULT createShaderResult = dx11Device->CreateComputeShader(data, shaderBlob.size, nullptr, (ID3D11ComputeShader**)&outPipeline->pipeline);`r`n        if (FAILED(createShaderResult))`r`n        {`r`n            delete[] data;`r`n            return FFX_ERROR_BACKEND_API_ERROR;`r`n        }" `
        -New "        g_lastCreateComputeShaderResultDX11 = S_OK;`r`n        const HRESULT createShaderResult = dx11Device->CreateComputeShader(data, shaderBlob.size, nullptr, (ID3D11ComputeShader**)&outPipeline->pipeline);`r`n        if (FAILED(createShaderResult))`r`n        {`r`n            g_lastCreateComputeShaderResultDX11 = createShaderResult;`r`n            delete[] data;`r`n            return FFX_ERROR_BACKEND_API_ERROR;`r`n        }"
    Set-PatchedText -Path $dx11BackendPath -Text $dx11Backend
}

if (-not (Test-Path $sdkRoot)) {
    throw "FidelityFX SDK source is missing. Run tools\fetch-fidelityfx-sdk-dx11.ps1 first."
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $vswherePaths = @(
        (Join-Path -Path ${env:ProgramFiles(x86)} -ChildPath 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path -Path $env:ProgramFiles -ChildPath 'Microsoft Visual Studio\Installer\vswhere.exe')
    )

    foreach ($vswherePath in $vswherePaths) {
        if (-not (Test-Path -LiteralPath $vswherePath)) {
            continue
        }

        $vsInstallPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if (-not $vsInstallPath) {
            continue
        }

        $vsCmakePath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $vsCmakePath) {
            $cmake = Get-Item -LiteralPath $vsCmakePath
            break
        }
    }
}

if (-not $cmake) {
    throw 'CMake is required to build the FidelityFX SDK static libraries, but cmake.exe could not be found.'
}

$cmakePath = if ($cmake.Source) { $cmake.Source } else { $cmake.FullName }

if ($Parallel -lt 1) {
    throw 'Parallel must be 1 or greater.'
}

if ($Force -and (Test-Path $buildRoot)) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}

Patch-FidelityFxDx11Source

$configureArgs = @(
    '-S', $sdkRoot,
    '-B', $buildRoot,
    '-A', 'x64',
    '-DFFX_API_CUSTOM=OFF',
    '-DFFX_API_VK=OFF',
    '-DFFX_API_DX12=OFF',
    '-DFFX_ALL=OFF',
    '-DFFX_API_DX11=ON',
    '-DFFX_FSR2=ON',
    '-DFFX_FSR3=OFF',
    '-DFFX_FSR3UPSCALER=OFF',
    '-DFFX_FI=OFF',
    '-DFFX_OF=OFF',
    '-DFFX_AUTO_COMPILE_SHADERS=1'
)

Write-Host 'Configuring FidelityFX SDK DX11 FSR2 static libraries...'
& $cmakePath @configureArgs

Write-Host "Building FidelityFX SDK ($Configuration)..."
& $cmakePath --build $buildRoot --config $Configuration --parallel $Parallel -- "/p:CL_MPcount=$Parallel"

$debugPostfix = if ($Configuration -eq 'Debug') { 'd' } else { '' }
$requiredLibs = @(
    "ffx_backend_dx11_x64$debugPostfix.lib",
    "ffx_fsr2_x64$debugPostfix.lib"
)

$missingLibs = @()
foreach ($lib in $requiredLibs) {
    $path = Join-Path $libRoot $lib
    if (-not (Test-Path $path)) {
        $missingLibs += $lib
    }
}

if ($missingLibs.Count -gt 0) {
    throw "FidelityFX SDK build finished, but required libraries are missing from $libRoot`: $($missingLibs -join ', ')"
}

Write-Host 'FidelityFX SDK libraries are ready:'
foreach ($lib in $requiredLibs) {
    Write-Host "  $(Join-Path $libRoot $lib)"
}
