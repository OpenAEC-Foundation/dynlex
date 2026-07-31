Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "windows-toolchain.ps1")
. (Join-Path $PSScriptRoot "llvm-mingw-toolchain.ps1")

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

function Resolve-VsWhereExecutable {
    $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\Installer\vswhere.exe")
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Find-VisualStudioCppToolchain {
    param(
        [ValidateSet("x64", "arm64")]
        [string]$TargetArchitecture
    )

    $vswhere = Resolve-VsWhereExecutable
    if (-not $vswhere) {
        return $null
    }
    $toolComponent = if ($TargetArchitecture -eq "arm64") {
        "Microsoft.VisualStudio.Component.VC.Tools.ARM64"
    } else {
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    }
    $installations = @(
        & $vswhere `
            -latest `
            -products * `
            -requires $toolComponent `
            -property installationPath |
            Where-Object { $_ }
    )
    if ($LASTEXITCODE -ne 0 -or $installations.Count -ne 1) {
        return $null
    }
    return $installations[0]
}

function Find-WindowsSdk {
    param(
        [ValidateSet("x64", "arm64")]
        [string]$TargetArchitecture
    )

    foreach ($kitsRoot in @(
        (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"),
        (Join-Path ${env:ProgramFiles} "Windows Kits\10")
    )) {
        $includeRoot = Join-Path $kitsRoot "Include"
        if (-not (Test-Path -LiteralPath $includeRoot -PathType Container)) {
            continue
        }
        foreach ($includeDirectory in @(
            Get-ChildItem -LiteralPath $includeRoot -Directory |
                Sort-Object Name -Descending
        )) {
            $version = $includeDirectory.Name
            if (
                (Test-Path -LiteralPath (Join-Path $includeDirectory.FullName "um\Windows.h") -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $includeDirectory.FullName "ucrt\stdio.h") -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $kitsRoot "Lib\$version\um\$TargetArchitecture\kernel32.lib") -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $kitsRoot "Lib\$version\ucrt\$TargetArchitecture\ucrt.lib") -PathType Leaf)
            ) {
                return "$kitsRoot ($version)"
            }
        }
    }
    return $null
}

function Test-VisualStudioCppToolchain {
    param(
        [ValidateSet("x64", "arm64")]
        [string]$TargetArchitecture
    )

    $visualStudio = Find-VisualStudioCppToolchain -TargetArchitecture $TargetArchitecture
    $windowsSdk = Find-WindowsSdk -TargetArchitecture $TargetArchitecture
    if ($visualStudio -and $windowsSdk) {
        return [PSCustomObject]@{
            VisualStudio = $visualStudio
            WindowsSdk = $windowsSdk
        }
    }
    return $null
}

function Ensure-VisualStudioCppToolchain {
    param(
        [ValidateSet("x64", "arm64")]
        [string]$TargetArchitecture
    )

    $toolchain = Test-VisualStudioCppToolchain -TargetArchitecture $TargetArchitecture
    if ($toolchain) {
        Write-Host `
            "Using Visual Studio C++ tools from $($toolchain.VisualStudio) and SDK $($toolchain.WindowsSdk)." `
            -ForegroundColor DarkGray
        return
    }
    if (-not (Test-Winget)) {
        throw "Visual Studio C++ Build Tools and a Windows SDK are required. Install the Desktop development with C++ workload and retry."
    }

    $installerOverride = @(
        "--wait",
        "--passive",
        "--norestart",
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--includeRecommended"
    )
    if ($TargetArchitecture -eq "arm64") {
        $installerOverride += @(
            "--add", "Microsoft.VisualStudio.Component.VC.Tools.ARM64"
        )
    }
    Write-Host "Installing Visual Studio C++ Build Tools and Windows SDK..." `
        -ForegroundColor Cyan
    winget install `
        --id Microsoft.VisualStudio.2022.BuildTools `
        --exact `
        --force `
        --silent `
        --disable-interactivity `
        --source winget `
        --accept-source-agreements `
        --accept-package-agreements `
        --override ($installerOverride -join " ")
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install Visual Studio C++ Build Tools."
    }

    $toolchain = Test-VisualStudioCppToolchain -TargetArchitecture $TargetArchitecture
    if (-not $toolchain) {
        throw "Visual Studio C++ Build Tools installation did not provide the required compiler and Windows SDK."
    }
}

function Ensure-Vcpkg {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $metadataPath = Join-Path $ProjectRoot "metadata\VCPKG_TOOLCHAIN"
    $metadata = @{}
    foreach ($line in Get-Content -LiteralPath $metadataPath) {
        if ($line -notmatch '^([a-z_]+) ([^ ]+)$') {
            throw "Invalid vcpkg toolchain metadata line: $line"
        }
        if ($metadata.ContainsKey($Matches[1])) {
            throw "Duplicate vcpkg toolchain metadata field: $($Matches[1])"
        }
        $metadata[$Matches[1]] = $Matches[2]
    }
    $expectedFields = @("repository", "release", "commit", "schema")
    $actualFields = @($metadata.Keys | Sort-Object)
    if (Compare-Object $actualFields ($expectedFields | Sort-Object)) {
        throw "vcpkg toolchain metadata fields do not match the required schema."
    }
    if ($metadata.repository -ne "https://github.com/microsoft/vcpkg.git" -or
        $metadata.schema -ne "1" -or
        $metadata.commit -notmatch '^[0-9a-f]{40}$') {
        throw "vcpkg toolchain metadata contains an unsupported repository, schema, or commit."
    }

    $manifestPath = Join-Path $ProjectRoot "vcpkg.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.'builtin-baseline' -ne $metadata.commit) {
        throw "vcpkg.json must use the commit pinned by metadata/VCPKG_TOOLCHAIN."
    }
    $manifestDependencies = @($manifest.dependencies | ForEach-Object {
        if ($_ -is [string]) { $_ } else { $_.name }
    })
    foreach ($dependency in @("freetype", "glfw3", "nlohmann-json")) {
        if ($dependency -notin $manifestDependencies) {
            throw "vcpkg.json is missing required dependency '$dependency'."
        }
    }

    $vcpkgRoot = Join-Path $ProjectRoot ".cache\vcpkg\$($metadata.release)"
    $git = Resolve-GitExecutable
    if (-not (Test-Path $vcpkgRoot)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $vcpkgRoot) -Force |
            Out-Null
        Write-Host "Installing pinned vcpkg $($metadata.release) at $vcpkgRoot..." `
            -ForegroundColor Cyan
        & $git clone `
            --branch $metadata.release `
            --depth 1 `
            $metadata.repository `
            $vcpkgRoot 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to clone pinned vcpkg release $($metadata.release)."
        }
    }

    $actualCommit = (& $git -C $vcpkgRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $metadata.commit) {
        throw "vcpkg checkout is $actualCommit, expected $($metadata.commit)."
    }
    $vcpkg = Join-Path $vcpkgRoot "vcpkg.exe"
    if (-not (Test-Path $vcpkg)) {
        & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics 2>&1 |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to bootstrap pinned vcpkg."
        }
    }
    return [PSCustomObject]@{
        Root = $vcpkgRoot
        Executable = $vcpkg
        Commit = $metadata.commit
    }
}

function Install-VcpkgDependencies {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $windowsTriplet = switch ($architecture) {
        "X64" { "x64-windows-static-crt" }
        "Arm64" { "arm64-windows-static-crt" }
        default { throw "Unsupported Windows architecture for DynLex dependencies: $architecture" }
    }

    $vcpkgToolchain = Ensure-Vcpkg -ProjectRoot $ProjectRoot
    $installationRoot = Join-Path `
        $ProjectRoot `
        ".cache\vcpkg-installed\$($vcpkgToolchain.Commit)"
    $tripletRoot = Join-Path $ProjectRoot "cmake\vcpkg-triplets"
    $env:VCPKG_ROOT = $vcpkgToolchain.Root
    Write-Host "Installing DynLex libraries through vcpkg..." -ForegroundColor Cyan
    Push-Location $ProjectRoot
    try {
        & $vcpkgToolchain.Executable install `
            --triplet $windowsTriplet `
            "--overlay-triplets=$tripletRoot" `
            "--x-install-root=$installationRoot" `
            --disable-metrics 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg failed to install the DynLex libraries."
        }
    } finally {
        Pop-Location
    }

    $windowsRoot = Join-Path $installationRoot $windowsTriplet
    $nlohmannJsonDir = Join-Path $windowsRoot "share\nlohmann_json"
    if (-not (Test-Path (Join-Path $windowsRoot "include\nlohmann\json.hpp")) -or
        -not (Test-Path (Join-Path $nlohmannJsonDir "nlohmann_jsonConfig.cmake"))) {
        throw "vcpkg reported success but the nlohmann_json package is incomplete in $windowsRoot."
    }

    $nativeLibraryDir = Join-Path $windowsRoot "lib"
    $nativeBinDir = Join-Path $windowsRoot "bin"
    $glfwImportLibrary = Get-ChildItem -Path $nativeLibraryDir -Filter "*glfw3*.lib" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $freetypeImportLibrary = Get-ChildItem -Path $nativeLibraryDir -Filter "*freetype*.lib" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $glfwRuntime = Get-ChildItem -Path $nativeBinDir -Filter "*glfw3*.dll" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $freetypeRuntime = Get-ChildItem -Path $nativeBinDir -Filter "*freetype*.dll" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (
        -not $glfwImportLibrary -or
        -not $freetypeImportLibrary -or
        -not $glfwRuntime -or
        -not $freetypeRuntime
    ) {
        throw "vcpkg reported success but the native GLFW or FreeType artifacts are incomplete in $windowsRoot."
    }

    $nlohmannJsonDirUnix = $nlohmannJsonDir -replace '\\', '/'
    $env:NLOHMANN_JSON_DIR = $nlohmannJsonDirUnix
    $env:PATH = "$nativeBinDir;$env:PATH"
    $env:LIB = if ($env:LIB) {
        "$nativeLibraryDir;$env:LIB"
    } else {
        $nativeLibraryDir
    }
    $env:LIBRARY_PATH = if ($env:LIBRARY_PATH) {
        "$nativeLibraryDir;$env:LIBRARY_PATH"
    } else {
        $nativeLibraryDir
    }
    [Environment]::SetEnvironmentVariable("NLOHMANN_JSON_DIR", $nlohmannJsonDirUnix, "User")
    Add-GitHubPathIfPresent -PathValue $nativeBinDir
    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "NLOHMANN_JSON_DIR=$nlohmannJsonDirUnix"
        Add-Content -Path $env:GITHUB_ENV -Value "LIB=$env:LIB"
        Add-Content -Path $env:GITHUB_ENV -Value "LIBRARY_PATH=$env:LIBRARY_PATH"
    }

    return [PSCustomObject]@{
        Root = $windowsRoot
        Triplet = $windowsTriplet
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

$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyArchitecture = switch ($hostArchitecture) {
    "X64" { "x64" }
    "Arm64" { "arm64" }
    default { throw "Unsupported Windows dependency architecture: $hostArchitecture" }
}
Ensure-VisualStudioCppToolchain -TargetArchitecture $dependencyArchitecture
$vcpkgDependencies = Install-VcpkgDependencies -ProjectRoot $projectRoot
$llvmMingw = Install-LlvmMingwToolchain `
    -ProjectRoot $projectRoot `
    -HostArchitecture $hostArchitecture
$env:DYNLEX_WINDOWS_TOOLCHAIN_ROOT = $llvmMingw.Root
$env:DYNLEX_WINDOWS_TOOLCHAIN_TARGET = $llvmMingw.TargetTriple
$env:DYNLEX_WINDOWS_DEPENDENCY_ROOT = $vcpkgDependencies.Root
if ($env:GITHUB_ENV) {
    Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_WINDOWS_TOOLCHAIN_ROOT=$($llvmMingw.Root)"
    Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_WINDOWS_TOOLCHAIN_TARGET=$($llvmMingw.TargetTriple)"
    Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_WINDOWS_DEPENDENCY_ROOT=$($vcpkgDependencies.Root)"
    Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_WINDOWS_TOOLCHAIN_ARCHITECTURE=$($llvmMingw.TargetArchitecture)"
    Add-Content -Path $env:GITHUB_ENV -Value "DYNLEX_WINDOWS_TOOLCHAIN_LLVM_VERSION=$($llvmMingw.LlvmVersion)"
}

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "The pinned LLVM fork is compiled and cached by scripts/build.sh."
Write-Host "Use these environment variables when building in this shell:"
Write-Host ('$env:PATH="' + $clangBin + ';$env:PATH"')
Write-Host ('$env:NLOHMANN_JSON_DIR="' + $vcpkgDependencies.NlohmannJsonDir + '"')
Write-Host ('$env:LIB="' + $vcpkgDependencies.NativeLibraryDir + ';$env:LIB"')
Write-Host ('$env:LIBRARY_PATH="' + $vcpkgDependencies.NativeLibraryDir + ';$env:LIBRARY_PATH"')
Write-Host ('$env:PATH="' + $vcpkgDependencies.NativeBinDir + ';$env:PATH"')
Write-Host ('$env:DYNLEX_WINDOWS_TOOLCHAIN_ROOT="' + $llvmMingw.Root + '"')
Write-Host ('$env:DYNLEX_WINDOWS_TOOLCHAIN_TARGET="' + $llvmMingw.TargetTriple + '"')
Write-Host ('$env:DYNLEX_WINDOWS_DEPENDENCY_ROOT="' + $vcpkgDependencies.Root + '"')
Write-Host "Build with: ./scripts/build.sh"
