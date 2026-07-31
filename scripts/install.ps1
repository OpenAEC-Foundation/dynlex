Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "windows-toolchain.ps1")

function Test-Winget {
    return [bool](Get-Command winget -ErrorAction SilentlyContinue)
}

function Install-WithWinget {
    param(
        [string]$PackageId,

        [ValidateSet("x64", "x86", "arm64")]
        [string]$Architecture
    )

    if (-not (Test-Winget)) {
        throw "winget is required to install missing package '$PackageId'. Install App Installer and retry."
    }
    $wingetArguments = @(
        "install",
        "--id", $PackageId,
        "--exact",
        "--silent",
        "--disable-interactivity",
        "--source", "winget",
        "--accept-source-agreements",
        "--accept-package-agreements"
    )
    if ($Architecture) {
        $wingetArguments += @("--architecture", $Architecture)
    }
    winget @wingetArguments
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install package '$PackageId'."
    }
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:PATH = "$machinePath;$userPath;$env:PATH"
}

function Ensure-Package {
    param(
        [string]$PackageId,
        [string]$CommandName
    )

    if (Get-Command $CommandName -ErrorAction SilentlyContinue) {
        Write-Host "Using existing $CommandName; skipping $PackageId." -ForegroundColor DarkGray
        return
    }
    Install-WithWinget $PackageId
}

function Add-GitHubPathIfPresent {
    param([string]$PathValue)

    if ($PathValue -and (Test-Path $PathValue) -and $env:GITHUB_PATH) {
        Add-Content -Path $env:GITHUB_PATH -Value $PathValue
    }
}

function Resolve-ToolDirectory {
    param(
        [string]$CommandName,
        [string[]]$CandidateDirectories
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return Split-Path -Parent $command.Source
    }
    foreach ($candidate in $CandidateDirectories) {
        if ($candidate -and (Test-Path (Join-Path $candidate "$CommandName.exe"))) {
            return $candidate
        }
    }
    throw "$CommandName was installed but could not be located."
}

function Resolve-GitExecutable {
    $directory = Resolve-ToolDirectory -CommandName "git" -CandidateDirectories @(
        (Join-Path ${env:ProgramFiles} "Git\cmd"),
        (Join-Path ${env:ProgramFiles} "Git\bin"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\Git\cmd")
    )
    return Join-Path $directory "git.exe"
}

function Resolve-VcpkgRoot {
    $candidates = @()
    $vcpkgCommand = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgCommand) {
        $candidates += Split-Path -Parent $vcpkgCommand.Source
    }
    if ($env:VCPKG_ROOT) {
        $candidates += $env:VCPKG_ROOT
    }
    if ($env:VCPKG_INSTALLATION_ROOT) {
        $candidates += $env:VCPKG_INSTALLATION_ROOT
    }
    $candidates += "C:\vcpkg"
    $candidates += Join-Path $env:LOCALAPPDATA "DynLex\vcpkg"

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if ($candidate -and (Test-Path (Join-Path $candidate "vcpkg.exe"))) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

function Ensure-Vcpkg {
    $vcpkgRoot = Resolve-VcpkgRoot
    if ($vcpkgRoot) {
        return $vcpkgRoot
    }

    $vcpkgRoot = Join-Path $env:LOCALAPPDATA "DynLex\vcpkg"
    if (Test-Path $vcpkgRoot) {
        throw "The vcpkg directory exists but is incomplete: $vcpkgRoot"
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $vcpkgRoot) -Force | Out-Null
    Write-Host "Installing vcpkg at $vcpkgRoot..." -ForegroundColor Cyan
    $git = Resolve-GitExecutable
    & $git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkgRoot 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone vcpkg."
    }

    & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to bootstrap vcpkg."
    }
    return $vcpkgRoot
}

function Install-VcpkgDependencies {
    $architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $windowsTriplet = switch ($architecture) {
        "X64" { "x64-windows" }
        "X86" { "x86-windows" }
        "Arm64" { "arm64-windows" }
        default { throw "Unsupported Windows architecture for DynLex dependencies: $architecture" }
    }
    $mingwTriplet = switch ($architecture) {
        "X64" { "x64-mingw-dynamic" }
        "X86" { "x86-mingw-dynamic" }
        "Arm64" { "arm64-mingw-dynamic" }
        default { throw "Unsupported Windows architecture for DynLex dependencies: $architecture" }
    }

    $vcpkgRoot = Ensure-Vcpkg
    $vcpkg = Join-Path $vcpkgRoot "vcpkg.exe"
    Write-Host "Installing DynLex libraries through vcpkg..." -ForegroundColor Cyan
    & $vcpkg install `
        "nlohmann-json:$windowsTriplet" `
        "glfw3:$mingwTriplet" `
        "freetype:$mingwTriplet" `
        --disable-metrics 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install the DynLex libraries."
    }

    $windowsRoot = Join-Path $vcpkgRoot "installed\$windowsTriplet"
    $nlohmannJsonDir = Join-Path $windowsRoot "share\nlohmann_json"
    if (-not (Test-Path (Join-Path $windowsRoot "include\nlohmann\json.hpp")) -or
        -not (Test-Path (Join-Path $nlohmannJsonDir "nlohmann_jsonConfig.cmake"))) {
        throw "vcpkg reported success but the nlohmann_json package is incomplete in $windowsRoot."
    }

    $mingwRoot = Join-Path $vcpkgRoot "installed\$mingwTriplet"
    $nativeLibraryDir = Join-Path $mingwRoot "lib"
    $nativeBinDir = Join-Path $mingwRoot "bin"
    $glfwImportLibrary = Get-ChildItem -Path $nativeLibraryDir -Filter "*glfw3*.a" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $freetypeImportLibrary = Get-ChildItem -Path $nativeLibraryDir -Filter "*freetype*.a" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $glfwImportLibrary -or -not $freetypeImportLibrary -or -not (Test-Path $nativeBinDir)) {
        throw "vcpkg reported success but the MinGW GLFW or FreeType artifacts are incomplete in $mingwRoot."
    }

    $nlohmannJsonDirUnix = $nlohmannJsonDir -replace '\\', '/'
    $env:NLOHMANN_JSON_DIR = $nlohmannJsonDirUnix
    $env:PATH = "$nativeBinDir;$env:PATH"
    $env:LIBRARY_PATH = if ($env:LIBRARY_PATH) {
        "$nativeLibraryDir;$env:LIBRARY_PATH"
    } else {
        $nativeLibraryDir
    }
    [Environment]::SetEnvironmentVariable("NLOHMANN_JSON_DIR", $nlohmannJsonDirUnix, "User")
    Add-GitHubPathIfPresent -PathValue $nativeBinDir
    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "NLOHMANN_JSON_DIR=$nlohmannJsonDirUnix"
        Add-Content -Path $env:GITHUB_ENV -Value "LIBRARY_PATH=$env:LIBRARY_PATH"
    }

    return [PSCustomObject]@{
        NlohmannJsonDir = $nlohmannJsonDir
        NativeLibraryDir = $nativeLibraryDir
        NativeBinDir = $nativeBinDir
    }
}

Write-Host "Installing DynLex build dependencies for Windows..." -ForegroundColor Cyan
$llvmMetadataPath = Join-Path (Split-Path -Parent $PSScriptRoot) "metadata\LLVM_TOOLCHAIN"
$bootstrapMetadata = @(Get-Content $llvmMetadataPath | Where-Object { $_ -match '^bootstrap [0-9]+$' })
if ($bootstrapMetadata.Count -ne 1) {
    throw "LLVM toolchain metadata must define exactly one bootstrap Clang version."
}
$bootstrapClangVersion = [int]($bootstrapMetadata[0] -split ' ')[1]
$hostArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()

$clangBin = Resolve-CompatibleClangDirectory `
    -MinimumMajorVersion $bootstrapClangVersion `
    -HostArchitecture $hostArchitecture
if (-not $clangBin) {
    Install-WithWinget "LLVM.LLVM" -Architecture ($hostArchitecture.ToLowerInvariant())
    $clangBin = Resolve-CompatibleClangDirectory `
        -MinimumMajorVersion $bootstrapClangVersion `
        -HostArchitecture $hostArchitecture
    if (-not $clangBin) {
        throw "LLVM.LLVM did not provide Clang $bootstrapClangVersion or newer."
    }
} else {
    Write-Host "Using compatible Clang from $clangBin; skipping LLVM.LLVM." `
        -ForegroundColor DarkGray
}

Ensure-Package "Kitware.CMake" "cmake"
Ensure-Package "Ninja-build.Ninja" "ninja"
Ensure-Package "Git.Git" "git"
Ensure-Package "Python.Python.3" "python"
Ensure-Package "OpenJS.NodeJS.LTS" "node"
Ensure-Package "GoLang.Go" "go"

$env:PATH = "$clangBin;$env:PATH"
Add-GitHubPathIfPresent -PathValue $clangBin
Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "CMake\bin")
Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Git\bin")
Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "nodejs")
Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Go\bin")

$vcpkgDependencies = Install-VcpkgDependencies

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "The pinned LLVM fork is compiled and cached by scripts/build.sh."
Write-Host "Use these environment variables when building in this shell:"
Write-Host ('$env:PATH="' + $clangBin + ';$env:PATH"')
Write-Host ('$env:NLOHMANN_JSON_DIR="' + $vcpkgDependencies.NlohmannJsonDir + '"')
Write-Host ('$env:LIBRARY_PATH="' + $vcpkgDependencies.NativeLibraryDir + ';$env:LIBRARY_PATH"')
Write-Host ('$env:PATH="' + $vcpkgDependencies.NativeBinDir + ';$env:PATH"')
Write-Host "Build with: ./scripts/build.sh"
