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

function Test-CommandAvailable {
    param([string]$CommandName)

    return [bool](Get-Command $CommandName -ErrorAction SilentlyContinue)
}

function Ensure-Package {
    param(
        [string]$PackageId,
        [string]$CommandName
    )

    if ($CommandName -and (Test-CommandAvailable $CommandName)) {
        Write-Host "Using existing $CommandName; skipping $PackageId." -ForegroundColor DarkGray
        return
    }

    Install-WithWinget $PackageId
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

function Get-LlvmRootFromConfig {
    param([System.IO.FileInfo]$ConfigFile)

    if (-not $ConfigFile) {
        return $null
    }

    return Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ConfigFile.FullName))
}

function Resolve-LlvmConfigFromBin {
    param([string]$LlvmBinPath)

    if (-not $LlvmBinPath) {
        return $null
    }

    $llvmRoot = Split-Path -Parent $LlvmBinPath
    $candidates = @(
        (Join-Path $llvmRoot "lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path $llvmRoot "lib64\cmake\llvm\LLVMConfig.cmake")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Get-Item $candidate)
        }
    }

    return $null
}

function Find-Vswhere {
    $vswhereCmd = Get-Command vswhere -ErrorAction SilentlyContinue
    if ($vswhereCmd) {
        return $vswhereCmd.Source
    }

    $installerPath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $installerPath) {
        return $installerPath
    }

    return $null
}

function Find-LlvmConfigInVisualStudio {
    $vswhere = Find-Vswhere
    if (-not $vswhere) {
        return $null
    }

    $installRoots = @()
    try {
        $instances = & $vswhere -all -products * -property installationPath 2>$null
        if ($instances) {
            $installRoots += $instances
        }
    } catch {
    }

    foreach ($root in $installRoots) {
        if (-not $root) {
            continue
        }

        $candidateCmakeDirs = @(
            (Join-Path $root "VC\Tools\Llvm\x64\lib\cmake\llvm"),
            (Join-Path $root "VC\Tools\Llvm\arm64\lib\cmake\llvm")
        )
        foreach ($cmakeDir in $candidateCmakeDirs) {
            $configPath = Join-Path $cmakeDir "LLVMConfig.cmake"
            if (Test-Path $configPath) {
                return (Get-Item $configPath)
            }
        }

        $llvmToolsRoot = Join-Path $root "VC\Tools\Llvm"
        if (Test-Path $llvmToolsRoot) {
            $config = Get-ChildItem -Path $llvmToolsRoot -Recurse -Filter LLVMConfig.cmake -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($config) {
                return $config
            }
        }
    }

    return $null
}

function Find-LlvmConfig {
    $visualStudioLlvm = Find-LlvmConfigInVisualStudio
    if ($visualStudioLlvm) {
        return $visualStudioLlvm
    }

    $searchRoots = @(
        (Join-Path ${env:ProgramFiles} "LLVM"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM"),
        (Join-Path ${env:ProgramData} "chocolatey\lib"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\LLVM"),
        (Join-Path ${env:LOCALAPPDATA} "Microsoft\WinGet\Packages")
    )

    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) {
            continue
        }
        $config = Get-ChildItem -Path $root -Recurse -Filter LLVMConfig.cmake -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($config) {
            return $config
        }
    }

    $llvmConfigExe = Get-Command llvm-config -ErrorAction SilentlyContinue
    if ($llvmConfigExe) {
        try {
            $cmakeDir = (& $llvmConfigExe.Source --cmakedir).Trim()
            if ($cmakeDir) {
                $configPath = Join-Path $cmakeDir "LLVMConfig.cmake"
                if (Test-Path $configPath) {
                    return (Get-Item $configPath)
                }
            }
        } catch {
        }
    }

    if ($env:CI -eq "true") {
        # Last resort in CI: broad scan to accommodate runner image layout changes.
        $config = Get-ChildItem -Path "C:\" -Recurse -Filter LLVMConfig.cmake -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($config) {
            return $config
        }
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

$llvmConfig = Find-LlvmConfig
if (-not $llvmConfig) {
    Install-WithWinget "LLVM.LLVM"
    $llvmConfig = Find-LlvmConfig
}

Ensure-Package "Kitware.CMake" "cmake"
Ensure-Package "Ninja-build.Ninja" "ninja"
Ensure-Package "Git.Git" "git"
Ensure-Package "Python.Python.3" "python"
Ensure-Package "OpenJS.NodeJS.LTS" "node"
Ensure-Package "GoLang.Go" "go"

$llvmBin = $null
if ($llvmConfig) {
    $llvmCmake = Split-Path -Parent $llvmConfig.FullName
    $llvmRoot = Get-LlvmRootFromConfig $llvmConfig
    $candidateBin = Join-Path $llvmRoot "bin"
    if (Test-Path $candidateBin) {
        $llvmBin = $candidateBin
    }
}

if (-not $llvmBin) {
    $llvmBin = Resolve-LlvmBin
    if ($llvmBin) {
        $configFromBin = Resolve-LlvmConfigFromBin $llvmBin
        if ($configFromBin) {
            $llvmConfig = $configFromBin
        }
    }
}

if ($llvmConfig -and $llvmBin) {
    $configRoot = Get-LlvmRootFromConfig $llvmConfig
    $binRoot = Split-Path -Parent $llvmBin
    if ($configRoot -ne $binRoot) {
        $configFromBin = Resolve-LlvmConfigFromBin $llvmBin
        if ($configFromBin) {
            $llvmConfig = $configFromBin
        } else {
            throw "Detected mismatched LLVM tools ($llvmBin) and LLVMConfig ($($llvmConfig.FullName)), and no matching LLVMConfig.cmake exists for the selected LLVM tools."
        }
    }
}

if ($llvmBin) {
    if (-not $llvmConfig) {
        throw "LLVMConfig.cmake was not found under the known LLVM install roots."
    }
    $llvmCmake = Split-Path -Parent $llvmConfig.FullName

    Add-GitHubPathIfPresent -PathValue $llvmBin
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "CMake\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Git\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "nodejs")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Go\bin")

    if ($env:GITHUB_ENV) {
        $llvmCmakeUnix = $llvmCmake -replace '\\', '/'
        Add-Content -Path $env:GITHUB_ENV -Value "LLVM_DIR=$llvmCmakeUnix"
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
