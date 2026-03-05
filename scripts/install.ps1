Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Admin {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Please run this script in an elevated PowerShell session (Run as Administrator)."
    }
}

function Install-With-Choco {
    param([string]$PackageName, [string]$Params = "")
    if ([string]::IsNullOrWhiteSpace($Params)) {
        choco install $PackageName -y --no-progress
        return
    }

    choco install $PackageName -y --no-progress --package-parameters $Params
}

if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
    throw "Chocolatey is required. Install it first: https://chocolatey.org/install"
}

Require-Admin

Write-Host "Installing DynLex build dependencies for Windows..." -ForegroundColor Cyan

Install-With-Choco llvm
Install-With-Choco cmake
Install-With-Choco ninja
Install-With-Choco git
Install-With-Choco python
Install-With-Choco nodejs-lts
Install-With-Choco golang

$llvmDir = Join-Path ${env:ProgramFiles} "LLVM\bin"
if (Test-Path $llvmDir) {
    Write-Host ""
    Write-Host "LLVM tools installed at: $llvmDir" -ForegroundColor Green
    Write-Host "Use these environment variables when building in this shell:"
    Write-Host '$env:PATH="'$llvmDir';$env:PATH"'
    Write-Host '$env:LLVM_DIR="'${env:ProgramFiles}'\LLVM\lib\cmake\llvm"'
}

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "Build with CMake (example):"
Write-Host '  cmake -S . -B build -G Ninja -DLLVM_DIR="$env:ProgramFiles\LLVM\lib\cmake\llvm"'
Write-Host "  cmake --build build --parallel"
