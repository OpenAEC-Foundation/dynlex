Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

[CmdletBinding()]
param(
    [switch]$Minimal
)

function Require-Winget {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is required but was not found. Install App Installer from Microsoft Store and retry."
    }
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

Require-Winget

Write-Host "Installing DynLex build dependencies for Windows..." -ForegroundColor Cyan

Install-WithWinget "LLVM.LLVM"
Install-WithWinget "Kitware.CMake"
Install-WithWinget "Ninja-build.Ninja"
Install-WithWinget "Git.Git"
if (-not $Minimal) {
    Install-WithWinget "Python.Python.3"
    Install-WithWinget "OpenJS.NodeJS.LTS"
    Install-WithWinget "GoLang.Go"
}

$llvmBin = Resolve-LlvmBin
if ($llvmBin) {
    $llvmRoot = Split-Path -Parent $llvmBin
    Write-Host ""
    Write-Host "LLVM tools installed at: $llvmBin" -ForegroundColor Green
    Write-Host "Use these environment variables when building in this shell:"
    Write-Host ('$env:PATH="' + $llvmBin + ';$env:PATH"')
    Write-Host ('$env:LLVM_DIR="' + $llvmRoot + '\lib\cmake\llvm"')
} else {
    Write-Warning "LLVM install path could not be auto-detected. Ensure clang is on PATH and LLVM_DIR is set before building."
}

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "Build with CMake (example):"
Write-Host '  cmake -S . -B build -G Ninja -DLLVM_DIR="$env:LLVM_DIR"'
Write-Host "  cmake --build build --parallel"
