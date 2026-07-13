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

    # Typical layout: <root>/lib/cmake/llvm/LLVMConfig.cmake
    # Also supports <root>/lib64/cmake/llvm/LLVMConfig.cmake.
    $llvmDir = Split-Path -Parent $ConfigFile.FullName
    $cmakeDir = Split-Path -Parent $llvmDir
    $libDir = Split-Path -Parent $cmakeDir
    $libLeaf = (Split-Path -Leaf $libDir).ToLowerInvariant()
    if ($libLeaf -eq "lib" -or $libLeaf -eq "lib64") {
        return Split-Path -Parent $libDir
    }

    return Split-Path -Parent $libDir
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

function Find-FirstFileByFilter {
    param(
        [string]$Root,
        [string]$Filter
    )

    if (-not (Test-Path $Root)) {
        return $null
    }

    return Get-ChildItem -Path $Root -Recurse -Filter $Filter -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        Select-Object -First 1
}

function Find-FirstLlvmConfigInRoot {
    param([string]$Root)

    $candidates = @()
    $firstUpper = Find-FirstFileByFilter -Root $Root -Filter "LLVMConfig.cmake"
    if ($firstUpper) {
        $candidates += $firstUpper
    }
    $firstLower = Find-FirstFileByFilter -Root $Root -Filter "llvm-config.cmake"
    if ($firstLower) {
        $candidates += $firstLower
    }

    if (@($candidates).Count -eq 0) {
        return $null
    }

    return @($candidates | Sort-Object FullName)[0]
}

function Find-LlvmConfigsInRoots {
    param([string[]]$Roots)

    $results = @()
    foreach ($root in $Roots) {
        $config = Find-FirstLlvmConfigInRoot -Root $root
        if ($config) {
            $results += $config.FullName
        }
    }

    return $results | Sort-Object -Unique
}

function Find-LlvmConfigInKnownLayouts {
    $candidates = @(
        (Join-Path ${env:ProgramFiles} "LLVM\lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramFiles} "LLVM\lib\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:ProgramFiles} "LLVM\lib64\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramFiles} "LLVM\lib64\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\lib\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\lib64\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\lib64\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:USERPROFILE} "Documents\LLVM\lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:USERPROFILE} "Documents\LLVM\lib\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:USERPROFILE} "Documents\LLVM\lib64\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:USERPROFILE} "Documents\LLVM\lib64\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\LLVM\lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\LLVM\lib\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\LLVM\lib64\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\LLVM\lib64\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:ProgramData} "chocolatey\lib\llvm\tools\llvm\lib\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramData} "chocolatey\lib\llvm\tools\llvm\lib\cmake\llvm\llvm-config.cmake"),
        (Join-Path ${env:ProgramData} "chocolatey\lib\llvm\tools\llvm\lib64\cmake\llvm\LLVMConfig.cmake"),
        (Join-Path ${env:ProgramData} "chocolatey\lib\llvm\tools\llvm\lib64\cmake\llvm\llvm-config.cmake")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return Get-Item $candidate
        }
    }

    return $null
}

function Find-LlvmConfigInGhcup {
    $patterns = @(
        "C:\ghcup\ghc\*\mingw\lib\cmake\llvm\LLVMConfig.cmake",
        "C:\ghcup\ghc\*\mingw\lib\cmake\llvm\llvm-config.cmake"
    )

    $matches = @()
    foreach ($pattern in $patterns) {
        $matches += Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    }

    if (@($matches).Count -eq 0) {
        return $null
    }

    return @($matches | Sort-Object FullName)[-1]
}

function Write-LlvmDiscoveryDiagnostics {
    Write-Warning "LLVM auto-discovery failed. Emitting diagnostic scan results."

    $commands = @("clang", "clang++", "llvm-config")
    foreach ($cmd in $commands) {
        $entry = Get-Command $cmd -ErrorAction SilentlyContinue
        if ($entry) {
            Write-Host ("Found command {0}: {1}" -f $cmd, $entry.Source) -ForegroundColor DarkYellow
        } else {
            Write-Host ("Command not found on PATH: {0}" -f $cmd) -ForegroundColor DarkYellow
        }
    }

    Write-Host "Skipping recursive root scans during diagnostics to keep CI responsive." -ForegroundColor DarkYellow

    $llvmRoot = Join-Path ${env:ProgramFiles} "LLVM"
    Write-Host ("Program Files LLVM root exists: {0}" -f (Test-Path $llvmRoot)) -ForegroundColor DarkYellow
    $llvmLib = Join-Path $llvmRoot "lib"
    Write-Host ("Program Files LLVM lib exists: {0}" -f (Test-Path $llvmLib)) -ForegroundColor DarkYellow
    if (Test-Path $llvmLib) {
        Write-Host "Program Files LLVM .lib entries (first 40):" -ForegroundColor DarkYellow
        Get-ChildItem -Path $llvmLib -Filter *.lib -File -ErrorAction SilentlyContinue |
            Sort-Object Name |
            Select-Object -First 40 |
            ForEach-Object {
                Write-Host ("  - {0}" -f $_.Name) -ForegroundColor DarkYellow
            }
    }
    $llvmCmake = Join-Path $llvmLib "cmake\llvm"
    Write-Host ("Program Files LLVM cmake dir exists: {0}" -f (Test-Path $llvmCmake)) -ForegroundColor DarkYellow
    if (Test-Path $llvmCmake) {
        Write-Host "Program Files LLVM cmake dir entries:" -ForegroundColor DarkYellow
        Get-ChildItem -Path $llvmCmake -File -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object {
            Write-Host ("  - {0}" -f $_.Name) -ForegroundColor DarkYellow
        }
    }
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
        Write-Warning ("vswhere query failed: {0}" -f $_.Exception.Message)
    }

    try {
        $found = & $vswhere -all -products * -find '**\LLVMConfig.cmake' 2>$null
        if ($found) {
            $first = @($found | Sort-Object)[0]
            if ($first -and (Test-Path $first)) {
                return Get-Item $first
            }
        }
        $foundLower = & $vswhere -all -products * -find '**\llvm-config.cmake' 2>$null
        if ($foundLower) {
            $firstLower = @($foundLower | Sort-Object)[0]
            if ($firstLower -and (Test-Path $firstLower)) {
                return Get-Item $firstLower
            }
        }
    } catch {
        Write-Warning ("vswhere -find for LLVMConfig.cmake failed: {0}" -f $_.Exception.Message)
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
            $lowerConfigPath = Join-Path $cmakeDir "llvm-config.cmake"
            if (Test-Path $lowerConfigPath) {
                return (Get-Item $lowerConfigPath)
            }
        }

        $llvmToolsRoot = Join-Path $root "VC\Tools\Llvm"
        $config = Find-FirstLlvmConfigInRoot -Root $llvmToolsRoot
        if ($config) {
            return $config
        }
    }

    return $null
}

function Find-LlvmConfig {
    $ghcupConfig = Find-LlvmConfigInGhcup
    if ($ghcupConfig) {
        return $ghcupConfig
    }

    $knownLayoutConfig = Find-LlvmConfigInKnownLayouts
    if ($knownLayoutConfig) {
        return $knownLayoutConfig
    }

    $visualStudioLlvm = Find-LlvmConfigInVisualStudio
    if ($visualStudioLlvm) {
        return $visualStudioLlvm
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
                $lowerConfigPath = Join-Path $cmakeDir "llvm-config.cmake"
                if (Test-Path $lowerConfigPath) {
                    return (Get-Item $lowerConfigPath)
                }
            }
        } catch {
            Write-Warning ("llvm-config --cmakedir failed: {0}" -f $_.Exception.Message)
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

function Resolve-GitExecutable {
    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if ($gitCommand) {
        return $gitCommand.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles} "Git\cmd\git.exe"),
        (Join-Path ${env:ProgramFiles} "Git\bin\git.exe"),
        (Join-Path ${env:LOCALAPPDATA} "Programs\Git\cmd\git.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Git was installed but git.exe could not be located."
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

    $vcpkgParent = Split-Path -Parent $vcpkgRoot
    New-Item -ItemType Directory -Path $vcpkgParent -Force | Out-Null
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

function Install-NlohmannJson {
    $architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $triplet = switch ($architecture) {
        "X64" { "x64-windows" }
        "X86" { "x86-windows" }
        "Arm64" { "arm64-windows" }
        default { throw "Unsupported Windows architecture for nlohmann_json: $architecture" }
    }

    $vcpkgRoot = Ensure-Vcpkg
    $vcpkg = Join-Path $vcpkgRoot "vcpkg.exe"
    Write-Host "Installing nlohmann-json for $triplet..." -ForegroundColor Cyan
    & $vcpkg install "nlohmann-json:$triplet" --disable-metrics 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install nlohmann-json:$triplet."
    }

    $installedRoot = Join-Path $vcpkgRoot "installed\$triplet"
    $includeFile = Join-Path $installedRoot "include\nlohmann\json.hpp"
    $cmakeDir = Join-Path $installedRoot "share\nlohmann_json"
    $cmakeConfig = Join-Path $cmakeDir "nlohmann_jsonConfig.cmake"
    if (-not (Test-Path $includeFile) -or -not (Test-Path $cmakeConfig)) {
        throw "vcpkg reported success but the nlohmann_json headers or CMake package are missing from $installedRoot."
    }

    $cmakeDirUnix = $cmakeDir -replace '\\', '/'
    $env:NLOHMANN_JSON_DIR = $cmakeDirUnix
    [Environment]::SetEnvironmentVariable("NLOHMANN_JSON_DIR", $cmakeDirUnix, "User")
    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "NLOHMANN_JSON_DIR=$cmakeDirUnix"
    }

    return $cmakeDir
}

Write-Host "Installing DynLex build dependencies for Windows..." -ForegroundColor Cyan

if (-not (Test-Winget)) {
    throw "winget is required but was not found. Install App Installer from Microsoft Store and retry."
}

$llvmConfig = Find-LlvmConfig
$llvmBin = Resolve-LlvmBin
if (-not $llvmConfig -and -not $llvmBin) {
    Install-WithWinget "LLVM.LLVM"
    $llvmConfig = Find-LlvmConfig
    $llvmBin = Resolve-LlvmBin
}

Ensure-Package "Kitware.CMake" "cmake"
Ensure-Package "Ninja-build.Ninja" "ninja"
Ensure-Package "Git.Git" "git"
Ensure-Package "Python.Python.3" "python"
Ensure-Package "OpenJS.NodeJS.LTS" "node"
Ensure-Package "GoLang.Go" "go"
$nlohmannJsonDir = Install-NlohmannJson

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
    $env:PATH = "$llvmBin;$env:PATH"
    Add-GitHubPathIfPresent -PathValue $llvmBin
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "CMake\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Git\bin")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "nodejs")
    Add-GitHubPathIfPresent -PathValue (Join-Path ${env:ProgramFiles} "Go\bin")

    if (-not $llvmConfig) {
        Write-LlvmDiscoveryDiagnostics
        Write-Warning "Proceeding without LLVM_DIR. CMake will attempt automatic LLVM package discovery."
    } else {
        $llvmCmake = Split-Path -Parent $llvmConfig.FullName
        $llvmCmakeUnix = $llvmCmake -replace '\\', '/'
        $env:LLVM_DIR = $llvmCmakeUnix
        $env:DYNLEX_LLVM_VERSION = "20"
        [Environment]::SetEnvironmentVariable("LLVM_DIR", $llvmCmakeUnix, "User")
        [Environment]::SetEnvironmentVariable("DYNLEX_LLVM_VERSION", "20", "User")
        if ($env:GITHUB_ENV) {
            Add-Content -Path $env:GITHUB_ENV -Value "LLVM_DIR=$llvmCmakeUnix"
            Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_LLVM_VERSION=20"
        }
    }

    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_LLVM_VERSION=20"
    }

    Write-Host ""
    Write-Host "LLVM tools installed at: $llvmBin" -ForegroundColor Green
    Write-Host "Use these environment variables when building in this shell:"
    Write-Host ('$env:PATH="' + $llvmBin + ';$env:PATH"')
    if ($llvmConfig) {
        Write-Host ('$env:LLVM_DIR="' + $llvmCmake + '"')
    } else {
        Write-Host '$env:LLVM_DIR should be set manually if CMake cannot auto-discover LLVM.'
    }
    Write-Host ('$env:NLOHMANN_JSON_DIR="' + $nlohmannJsonDir + '"')
} else {
    throw "LLVM install path could not be auto-detected. Ensure clang is on PATH and LLVM_DIR is set before building."
}

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "Build with CMake (example):"
Write-Host '  cmake -S . -B build -G Ninja -DLLVM_DIR="$env:LLVM_DIR" -Dnlohmann_json_DIR="$env:NLOHMANN_JSON_DIR"'
Write-Host "  cmake --build build --parallel"
