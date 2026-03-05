Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-Winget {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        return $false
    }
    return $true
}

function Install-WithWinget {
    param([string]$PackageId)

    winget install `
        --id $PackageId `
        --exact `
        --silent `
        --disable-interactivity `
        --source winget `
        --accept-source-agreements `
        --accept-package-agreements
}

function Resolve-LlvmBin {
    $llvmConfig = Get-Command llvm-config -ErrorAction SilentlyContinue
    if ($llvmConfig) {
        return Split-Path -Parent $llvmConfig.Source
    }

    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) {
        return Split-Path -Parent $clang.Source
    }

    $defaultLlvmBin = Join-Path ${env:ProgramFiles} "LLVM\bin"
    if (Test-Path $defaultLlvmBin) {
        return $defaultLlvmBin
    }

    return $null
}

function Add-GitHubPathIfPresent {
    param([string]$PathValue)

    if (-not $PathValue) {
        return
    }
    if (-not (Test-Path $PathValue)) {
        return
    }

    if ($env:GITHUB_PATH) {
        Add-Content -Path $env:GITHUB_PATH -Value $PathValue
    }
}

Write-Host "Installing DynLex build dependencies for Windows..." -ForegroundColor Cyan

if (-not (Test-Winget)) {
    throw "winget is required but was not found. Install App Installer from Microsoft Store and retry."
}

Install-WithWinget "LLVM.LLVM"
Install-WithWinget "Kitware.CMake"
Install-WithWinget "Ninja-build.Ninja"
Install-WithWinget "Git.Git"
Install-WithWinget "Python.Python.3"
Install-WithWinget "OpenJS.NodeJS.LTS"
Install-WithWinget "GoLang.Go"

$llvmBin = Resolve-LlvmBin
if ($llvmBin) {
    $llvmRoot = Split-Path -Parent $llvmBin
    $llvmCmake = Join-Path $llvmRoot "lib\cmake\llvm"

    Add-GitHubPathIfPresent -PathValue $llvmBin
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "CMake\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Git\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "nodejs")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Go\bin")

    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "LLVM_DIR=$llvmCmake"
        Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_LLVM_VERSION=20"
    }

    Write-Host ""
    Write-Host "LLVM tools installed at: $llvmBin" -ForegroundColor Green
    Write-Host "Use these environment variables when building in this shell:"
    Write-Host ('$env:PATH="' + $llvmBin + ';$env:PATH"')
    Write-Host ('$env:LLVM_DIR="' + $llvmCmake + '"')
} else {
    throw "LLVM install path could not be auto-detected. Ensure clang is on PATH and LLVM_DIR is set before building."
}

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "Build with CMake (example):"
Write-Host '  cmake -S . -B build -G Ninja -DLLVM_DIR="$env:LLVM_DIR"'
Write-Host "  cmake --build build --parallel"
