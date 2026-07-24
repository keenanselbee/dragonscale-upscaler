param(
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $PSScriptRoot 'dev-dependencies.json'
$cacheRoot = Join-Path $repoRoot 'tools\.cache'
$xmakePackageCache = Join-Path $cacheRoot 'xmake-packages'
$xmakeRoot = Join-Path $repoRoot 'dev\tools\xmake'
$xmakeExe = Join-Path $xmakeRoot 'xmake.exe'
$sevenZipRoot = Join-Path $repoRoot 'dev\tools\7z'
$sevenZipExe = Join-Path $sevenZipRoot '7z.exe'
$xmakeGlobalDir = Join-Path $repoRoot 'dev\.xmake'

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

function Get-RepoRelativePath([string]$Path) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $repoRoot).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside repository: $fullPath"
    }

    return $fullPath.Substring($fullRoot.Length).Replace('\', '/')
}

function Join-CommandLineArguments([string[]]$Arguments) {
    $escaped = foreach ($argument in $Arguments) {
        '"' + ($argument -replace '"', '\"') + '"'
    }

    return ($escaped -join ' ')
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [switch]$Quiet
    )

    $processInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $FilePath
    $processInfo.Arguments = Join-CommandLineArguments $Arguments
    $processInfo.UseShellExecute = $false
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::Start($processInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if (-not $Quiet) {
        if ($stdout) {
            Write-Host $stdout.TrimEnd()
        }
        if ($stderr) {
            Write-Host $stderr.TrimEnd()
        }
    }

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        StdOut = $stdout
        StdErr = $stderr
    }
}

function Test-GitIgnoredPath([string]$Path) {
    $relative = Get-RepoRelativePath $Path
    $result = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $repoRoot, 'check-ignore', '-q', '--', $relative) -Quiet
    return $result.ExitCode -eq 0
}

function Assert-IgnoredPathForRepair([string]$Path, [string]$Label) {
    Assert-PathInside $Path $repoRoot $Label
    if (-not (Test-GitIgnoredPath $Path)) {
        throw "$Label is not ignored by git, refusing to repair destructively: $Path"
    }
}

function Add-Issue([System.Collections.Generic.List[string]]$Issues, [string]$Message) {
    [void]$Issues.Add($Message)
    Write-Warning $Message
}

function Ensure-Directory([string]$Path) {
    if (-not $VerifyOnly -and -not (Test-Path -LiteralPath $Path -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Get-GitHead([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $null
    }

    $result = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $Path, 'rev-parse', 'HEAD') -Quiet
    if ($result.ExitCode -ne 0) {
        return $null
    }

    return (($result.StdOut -split "`r?`n") | Where-Object { $_ } | Select-Object -First 1)
}

function Ensure-Xmake([pscustomobject]$Xmake, [System.Collections.Generic.List[string]]$Issues) {
    if ($VerifyOnly) {
        if (-not (Test-Path -LiteralPath $xmakeExe -PathType Leaf)) {
            Add-Issue $Issues "xmake is missing: $xmakeExe"
            return
        }

        $hash = (Get-FileHash -LiteralPath $xmakeExe -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $Xmake.sha256.ToLowerInvariant()) {
            Add-Issue $Issues "xmake hash mismatch at $xmakeExe`: expected $($Xmake.sha256), got $hash"
        }
        return
    }

    Ensure-Directory $cacheRoot
    Ensure-Directory $xmakePackageCache
    Ensure-Directory $xmakeRoot
    Ensure-Directory $xmakeGlobalDir

    $cacheFile = Join-Path $cacheRoot (Split-Path -Leaf $Xmake.url)
    $needsDownload = $true
    if (Test-Path -LiteralPath $cacheFile -PathType Leaf) {
        $cacheHash = (Get-FileHash -LiteralPath $cacheFile -Algorithm SHA256).Hash.ToLowerInvariant()
        $needsDownload = $cacheHash -ne $Xmake.sha256.ToLowerInvariant()
        if ($needsDownload) {
            Write-Warning "Cached xmake hash mismatch; re-downloading $cacheFile"
        }
    }

    if ($needsDownload) {
        Write-Host "[INFO] Downloading xmake $($Xmake.version)..."
        Invoke-WebRequest -Uri $Xmake.url -OutFile $cacheFile -Headers @{ 'User-Agent' = 'DragonScale-Bootstrap/1.0' }
    } else {
        Write-Host "[INFO] Using cached xmake: $cacheFile"
    }

    $hash = (Get-FileHash -LiteralPath $cacheFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $Xmake.sha256.ToLowerInvariant()) {
        throw "Downloaded xmake hash mismatch: expected $($Xmake.sha256), got $hash"
    }

    Copy-Item -LiteralPath $cacheFile -Destination $xmakeExe -Force
    $versionOutput = & $xmakeExe --version
    if ($LASTEXITCODE -ne 0 -or (($versionOutput -join "`n") -notmatch [regex]::Escape($Xmake.version.TrimStart('v')))) {
        throw "Repo-local xmake did not report expected version $($Xmake.version)."
    }

    Write-Host "[INFO] xmake ready: $xmakeExe"
}

function Ensure-SevenZip([pscustomobject]$SevenZip, [System.Collections.Generic.List[string]]$Issues) {
    $cacheFileName = Split-Path -Leaf $SevenZip.url
    $cacheFile = Join-Path $xmakePackageCache $cacheFileName
    $xmakeAliasFile = Join-Path $xmakePackageCache "7z-$($SevenZip.version).zip"

    if ($VerifyOnly) {
        if (-not (Test-Path -LiteralPath $sevenZipExe -PathType Leaf)) {
            Add-Issue $Issues "7-Zip is missing: $sevenZipExe"
        }
        if (-not (Test-Path -LiteralPath $cacheFile -PathType Leaf)) {
            Add-Issue $Issues "7-Zip cache archive is missing: $cacheFile"
        } else {
            $hash = (Get-FileHash -LiteralPath $cacheFile -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($hash -ne $SevenZip.sha256.ToLowerInvariant()) {
                Add-Issue $Issues "7-Zip cache archive hash mismatch at $cacheFile`: expected $($SevenZip.sha256), got $hash"
            }
        }
        return
    }

    Ensure-Directory $xmakePackageCache
    Ensure-Directory $sevenZipRoot

    $needsDownload = $true
    if (Test-Path -LiteralPath $cacheFile -PathType Leaf) {
        $cacheHash = (Get-FileHash -LiteralPath $cacheFile -Algorithm SHA256).Hash.ToLowerInvariant()
        $needsDownload = $cacheHash -ne $SevenZip.sha256.ToLowerInvariant()
        if ($needsDownload) {
            Write-Warning "Cached 7-Zip hash mismatch; re-downloading $cacheFile"
        }
    }

    if ($needsDownload) {
        Write-Host "[INFO] Downloading 7-Zip $($SevenZip.version)..."
        Invoke-WebRequest -Uri $SevenZip.url -OutFile $cacheFile -Headers @{ 'User-Agent' = 'DragonScale-Bootstrap/1.0' }
    } else {
        Write-Host "[INFO] Using cached 7-Zip: $cacheFile"
    }

    $hash = (Get-FileHash -LiteralPath $cacheFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $SevenZip.sha256.ToLowerInvariant()) {
        throw "Downloaded 7-Zip hash mismatch: expected $($SevenZip.sha256), got $hash"
    }

    if ((-not (Test-Path -LiteralPath $xmakeAliasFile -PathType Leaf)) -or
        ((Get-FileHash -LiteralPath $xmakeAliasFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $SevenZip.sha256.ToLowerInvariant())) {
        Copy-Item -LiteralPath $cacheFile -Destination $xmakeAliasFile -Force
    }

    if (-not (Test-Path -LiteralPath $sevenZipExe -PathType Leaf)) {
        Assert-IgnoredPathForRepair $sevenZipRoot '7-Zip root'
        Remove-Item -LiteralPath $sevenZipRoot -Recurse -Force
        Ensure-Directory $sevenZipRoot
        Expand-Archive -LiteralPath $cacheFile -DestinationPath $sevenZipRoot -Force
    }

    $helpOutput = & $sevenZipExe --help
    if ($LASTEXITCODE -ne 0 -or (($helpOutput -join "`n") -notmatch '7-Zip')) {
        throw "Repo-local 7-Zip did not run correctly: $sevenZipExe"
    }

    Write-Host "[INFO] 7-Zip ready: $sevenZipExe"
}

function Ensure-GitRepository([pscustomobject]$Repo, [System.Collections.Generic.List[string]]$Issues) {
    $target = Join-Path $repoRoot $Repo.path
    Assert-PathInside $target $repoRoot $Repo.name
    $head = Get-GitHead $target

    if ($VerifyOnly) {
        if (-not $head) {
            Add-Issue $Issues "$($Repo.name) is missing or is not a valid git checkout: $target"
            return
        }
        if ($head -ne $Repo.commit) {
            Add-Issue $Issues "$($Repo.name) commit mismatch: expected $($Repo.commit), got $head"
        }
        return
    }

    if (-not $head) {
        if (Test-Path -LiteralPath $target) {
            Assert-IgnoredPathForRepair $target $Repo.name
            Write-Host "[INFO] Replacing invalid $($Repo.name) checkout: $target"
            Remove-Item -LiteralPath $target -Recurse -Force
        }

        Ensure-Directory (Split-Path -Parent $target)
        Write-Host "[INFO] Cloning $($Repo.name)..."
        $clone = Invoke-NativeCommand -FilePath 'git' -Arguments @('clone', $Repo.url, $target)
        if ($clone.ExitCode -ne 0) {
            throw "git clone failed for $($Repo.name)"
        }
    }

    Write-Host "[INFO] Updating $($Repo.name) to $($Repo.commit)..."
    $remote = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $target, 'remote', 'set-url', 'origin', $Repo.url)
    if ($remote.ExitCode -ne 0) {
        throw "git remote set-url failed for $($Repo.name)"
    }

    $fetch = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $target, 'fetch', '--tags', 'origin')
    if ($fetch.ExitCode -ne 0) {
        throw "git fetch failed for $($Repo.name)"
    }

    $checkout = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $target, 'checkout', '--force', $Repo.commit)
    if ($checkout.ExitCode -ne 0) {
        throw "git checkout failed for $($Repo.name)"
    }

    $reset = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $target, 'reset', '--hard', $Repo.commit)
    if ($reset.ExitCode -ne 0) {
        throw "git reset failed for $($Repo.name)"
    }

    $head = Get-GitHead $target
    if ($head -ne $Repo.commit) {
        throw "$($Repo.name) did not land on expected commit $($Repo.commit); got $head"
    }
}

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Dependency manifest not found: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$issues = [System.Collections.Generic.List[string]]::new()

Assert-PathInside $cacheRoot $repoRoot 'cache root'
Assert-PathInside $xmakePackageCache $repoRoot 'xmake package cache'
Assert-PathInside $xmakeRoot $repoRoot 'xmake root'
Assert-PathInside $xmakeGlobalDir $repoRoot 'xmake global dir'
Assert-PathInside $sevenZipRoot $repoRoot '7-Zip root'

Ensure-Xmake $manifest.xmake $issues
Ensure-SevenZip $manifest.sevenZip $issues

foreach ($repo in $manifest.repositories) {
    Ensure-GitRepository $repo $issues
}

if ($issues.Count -gt 0) {
    throw "DragonScale dev environment verification failed with $($issues.Count) issue(s)."
}

if ($VerifyOnly) {
    Write-Host '[SUCCESS] DragonScale dev environment verification passed.'
} else {
    Write-Host '[SUCCESS] DragonScale dev environment is ready.'
}
