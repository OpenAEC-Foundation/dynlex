Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-LlvmMingwMetadata {
    param([string]$ProjectRoot)

    $metadataPath = Join-Path $ProjectRoot "metadata\LLVM_MINGW_TOOLCHAIN"
    if (-not (Test-Path -PathType Leaf $metadataPath)) {
        throw "Missing LLVM MinGW toolchain metadata: $metadataPath"
    }

    $metadata = @{}
    foreach ($line in Get-Content $metadataPath) {
        if ($line -notmatch '^([a-z0-9_]+) (.+)$') {
            throw "Invalid LLVM MinGW toolchain metadata line: $line"
        }
        if ($metadata.ContainsKey($Matches[1])) {
            throw "Duplicate LLVM MinGW toolchain metadata field: $($Matches[1])"
        }
        $metadata[$Matches[1]] = $Matches[2]
    }

    $requiredFields = @(
        "repository",
        "release",
        "llvm",
        "crt",
        "x64_sha256",
        "arm64_sha256",
        "schema"
    )
    $actualFields = @($metadata.Keys | Sort-Object) -join "`n"
    $expectedFields = @($requiredFields | Sort-Object) -join "`n"
    if ($actualFields -ne $expectedFields) {
        throw "LLVM MinGW toolchain metadata must define exactly: $($requiredFields -join ', ')"
    }
    if ($metadata.repository -ne "https://github.com/mstorsjo/llvm-mingw") {
        throw "LLVM MinGW toolchain metadata names an unexpected repository."
    }
    if ($metadata.release -notmatch '^[0-9]{8}$' -or $metadata.llvm -notmatch '^[0-9]+$') {
        throw "LLVM MinGW release and LLVM version fields are invalid."
    }
    if ($metadata.crt -ne "ucrt" -or $metadata.schema -ne "3") {
        throw "LLVM MinGW CRT or metadata schema is unsupported."
    }
    foreach ($checksumField in "x64_sha256", "arm64_sha256") {
        if ($metadata[$checksumField] -notmatch '^[0-9a-f]{64}$') {
            throw "LLVM MinGW checksum field '$checksumField' is invalid."
        }
    }
    return $metadata
}

function Get-LlvmMingwHost {
    param([string]$HostArchitecture)

    switch ($HostArchitecture) {
        "X64" {
            return [PSCustomObject]@{
                AssetArchitecture = "x86_64"
                ChecksumField = "x64_sha256"
                TargetArchitecture = "x86_64"
                TargetTriple = "x86_64-w64-mingw32"
            }
        }
        "Arm64" {
            return [PSCustomObject]@{
                AssetArchitecture = "aarch64"
                ChecksumField = "arm64_sha256"
                TargetArchitecture = "aarch64"
                TargetTriple = "aarch64-w64-mingw32"
            }
        }
        default {
            throw "Unsupported Windows architecture for the LLVM MinGW toolchain: $HostArchitecture"
        }
    }
}

function Get-LlvmMingwCacheRoot {
    param([string]$ProjectRoot)

    if ($env:DYNLEX_LLVM_MINGW_CACHE_DIR) {
        return $env:DYNLEX_LLVM_MINGW_CACHE_DIR
    }
    return Join-Path $ProjectRoot ".cache\llvm-mingw"
}

function Assert-LlvmMingwLayout {
    param(
        [string]$ToolchainRoot,
        [string]$LlvmVersion,
        [string]$TargetArchitecture
    )

    $architectureIntrinsicHeader = switch ($TargetArchitecture) {
        "x86_64" { "x86intrin.h" }
        "aarch64" { "arm64intr.h" }
        default { throw "Unsupported LLVM MinGW target architecture: $TargetArchitecture" }
    }
    $requiredPaths = @(
        "LICENSE.TXT",
        "bin\cc.exe",
        "bin\clang-$LlvmVersion.exe",
        "bin\mingw32-common.cfg",
        "bin\$TargetArchitecture-w64-windows-gnu.cfg",
        "bin\llvm-ar.exe",
        "bin\ld.lld.exe",
        "bin\libLLVM-$LlvmVersion.dll",
        "bin\libclang-cpp.dll",
        "bin\libc++.dll",
        "bin\libunwind.dll",
        "lib\clang\$LlvmVersion\include\stdbool.h",
        "lib\clang\$LlvmVersion\include\stddef.h",
        "lib\clang\$LlvmVersion\include\$architectureIntrinsicHeader",
        "lib\clang\$LlvmVersion\lib\windows\libclang_rt.builtins-$TargetArchitecture.a",
        "include\windows.h",
        "include\winioctl.h",
        "include\shlobj.h",
        "include\sys\types.h",
        "$TargetArchitecture-w64-mingw32\lib\crt2.o",
        "$TargetArchitecture-w64-mingw32\lib\libkernel32.a",
        "$TargetArchitecture-w64-mingw32\share\mingw32\COPYING"
    )
    foreach ($relativePath in $requiredPaths) {
        if (-not (Test-Path -PathType Leaf (Join-Path $ToolchainRoot $relativePath))) {
            throw "LLVM MinGW toolchain is incomplete: missing $relativePath"
        }
    }
}

function Install-LlvmMingwToolchain {
    param(
        [string]$ProjectRoot,
        [string]$HostArchitecture
    )

    $metadata = Read-LlvmMingwMetadata -ProjectRoot $ProjectRoot
    $hostConfiguration = Get-LlvmMingwHost -HostArchitecture $HostArchitecture
    $assetName = "llvm-mingw-$($metadata.release)-$($metadata.crt)-$($hostConfiguration.AssetArchitecture).zip"
    $expectedChecksum = $metadata[$hostConfiguration.ChecksumField]
    $cacheRoot = Get-LlvmMingwCacheRoot -ProjectRoot $ProjectRoot
    $archiveDirectory = Join-Path $cacheRoot "downloads"
    $archivePath = Join-Path $archiveDirectory $assetName
    $toolchainRoot = Join-Path $cacheRoot "$($metadata.release)-$($metadata.crt)-$($hostConfiguration.AssetArchitecture)"
    $markerPath = Join-Path $toolchainRoot ".dynlex-llvm-mingw"
    $markerFields = @(
        "release=$($metadata.release)",
        "sha256=$expectedChecksum",
        "host=$($hostConfiguration.AssetArchitecture)",
        "target=$($hostConfiguration.TargetTriple)"
    )
    $markerContents = (@("schema=$($metadata.schema)") + $markerFields) -join "`n"
    $previousMarkerContents = @(
        (@("schema=1") + $markerFields) -join "`n"
        (@("schema=2") + $markerFields) -join "`n"
    )

    if (Test-Path $toolchainRoot) {
        if (-not (Test-Path -PathType Leaf $markerPath)) {
            throw "LLVM MinGW cache directory exists without its integrity marker: $toolchainRoot"
        }
        $actualMarkerContents = (Get-Content $markerPath -Raw).TrimEnd()
        if ($previousMarkerContents -contains $actualMarkerContents) {
            Remove-Item -Recurse -Force $toolchainRoot
        } elseif ($actualMarkerContents -ne $markerContents) {
            throw "LLVM MinGW cache integrity marker does not match the pinned toolchain."
        }
    }
    if (Test-Path $toolchainRoot) {
        Assert-LlvmMingwLayout `
            -ToolchainRoot $toolchainRoot `
            -LlvmVersion $metadata.llvm `
            -TargetArchitecture $hostConfiguration.TargetArchitecture
    } else {
        New-Item -ItemType Directory -Path $archiveDirectory -Force | Out-Null
        if (-not (Test-Path -PathType Leaf $archivePath)) {
            $assetUrl = "$($metadata.repository)/releases/download/$($metadata.release)/$assetName"
            Write-Host "Downloading pinned LLVM MinGW toolchain $assetName..." -ForegroundColor Cyan
            Invoke-WebRequest -Uri $assetUrl -OutFile $archivePath
        }
        $actualChecksum = (Get-FileHash -Algorithm SHA256 $archivePath).Hash.ToLowerInvariant()
        if ($actualChecksum -ne $expectedChecksum) {
            throw "LLVM MinGW archive checksum does not match metadata/LLVM_MINGW_TOOLCHAIN."
        }

        $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) "dynlex-llvm-mingw-$([guid]::NewGuid())"
        New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
        try {
            Expand-Archive -Path $archivePath -DestinationPath $temporaryRoot
            $extractedRoot = Join-Path $temporaryRoot "llvm-mingw-$($metadata.release)-$($metadata.crt)-$($hostConfiguration.AssetArchitecture)"
            $slimRoot = Join-Path $temporaryRoot "dynlex-toolchain"
            Assert-LlvmMingwLayout `
                -ToolchainRoot $extractedRoot `
                -LlvmVersion $metadata.llvm `
                -TargetArchitecture $hostConfiguration.TargetArchitecture
            $slimBin = Join-Path $slimRoot "bin"
            $slimInclude = Join-Path $slimRoot "include"
            $slimResourceRoot = Join-Path $slimRoot "lib\clang\$($metadata.llvm)"
            $slimResourceInclude = Join-Path $slimResourceRoot "include"
            $slimResourceLibraries = Join-Path $slimResourceRoot "lib\windows"
            $slimTarget = Join-Path $slimRoot "$($hostConfiguration.TargetTriple)"
            New-Item `
                -ItemType Directory `
                -Path $slimBin, $slimInclude, $slimResourceInclude, $slimResourceLibraries |
                Out-Null
            New-Item -ItemType Directory -Path (Join-Path $slimTarget "lib") | Out-Null
            New-Item -ItemType Directory -Path (Join-Path $slimTarget "share\mingw32") | Out-Null
            foreach ($fileName in @(
                "cc.exe",
                "clang-$($metadata.llvm).exe",
                "mingw32-common.cfg",
                "$($hostConfiguration.TargetArchitecture)-w64-windows-gnu.cfg",
                "llvm-ar.exe",
                "ld.lld.exe",
                "libLLVM-$($metadata.llvm).dll",
                "libclang-cpp.dll",
                "libc++.dll",
                "libunwind.dll"
            )) {
                Copy-Item (Join-Path $extractedRoot "bin\$fileName") $slimBin
            }
            Copy-Item (Join-Path $extractedRoot "LICENSE.TXT") $slimRoot
            Copy-Item `
                -Path (Join-Path $extractedRoot "lib\clang\$($metadata.llvm)\include\*") `
                -Destination $slimResourceInclude `
                -Recurse
            Copy-Item `
                (Join-Path $extractedRoot "lib\clang\$($metadata.llvm)\lib\windows\libclang_rt.builtins-$($hostConfiguration.TargetArchitecture).a") `
                $slimResourceLibraries
            Copy-Item `
                -Path (Join-Path $extractedRoot "include\*") `
                -Destination $slimInclude `
                -Recurse
            Copy-Item `
                (Join-Path $extractedRoot "$($hostConfiguration.TargetTriple)\lib\*") `
                (Join-Path $slimTarget "lib")
            Copy-Item `
                (Join-Path $extractedRoot "$($hostConfiguration.TargetTriple)\share\mingw32\*") `
                (Join-Path $slimTarget "share\mingw32")
            Set-Content -Path (Join-Path $slimRoot ".dynlex-llvm-mingw") -Value $markerContents -NoNewline
            Assert-LlvmMingwLayout `
                -ToolchainRoot $slimRoot `
                -LlvmVersion $metadata.llvm `
                -TargetArchitecture $hostConfiguration.TargetArchitecture
            Move-Item -Path $slimRoot -Destination $toolchainRoot
        } finally {
            if (Test-Path $temporaryRoot) {
                Remove-Item -Recurse -Force $temporaryRoot
            }
        }
    }

    return [PSCustomObject]@{
        Root = $toolchainRoot
        TargetArchitecture = $hostConfiguration.TargetArchitecture
        TargetTriple = $hostConfiguration.TargetTriple
        LlvmVersion = $metadata.llvm
    }
}
