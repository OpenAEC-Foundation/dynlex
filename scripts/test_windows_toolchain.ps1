Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "windows-toolchain.ps1")
. (Join-Path $PSScriptRoot "llvm-mingw-toolchain.ps1")

$developerEnvironment = ConvertFrom-WindowsCommandEnvironment -Lines @(
    "INCLUDE=C:\Visual Studio\include;C:\Windows Kits\include",
    "LIB=C:\Visual Studio\lib;C:\Windows Kits\lib",
    "LIBPATH=C:\Visual Studio\libpath",
    "VALUE=left=right",
    "=C:=C:\working-directory"
)
if (
    $developerEnvironment.Count -ne 4 -or
    $developerEnvironment.INCLUDE -ne "C:\Visual Studio\include;C:\Windows Kits\include" -or
    $developerEnvironment.LIB -ne "C:\Visual Studio\lib;C:\Windows Kits\lib" -or
    $developerEnvironment.LIBPATH -ne "C:\Visual Studio\libpath" -or
    $developerEnvironment.VALUE -ne "left=right" -or
    $developerEnvironment.ContainsKey("=C:")
) {
    throw "Windows command environment parsing lost developer toolchain variables."
}

$x64Metadata = ConvertFrom-ClangVersionOutput -VersionOutput @(
    "clang version 22.1.8"
    "Target: x86_64-pc-windows-msvc"
    "Thread model: posix"
    "InstalledDir: C:\Program Files\LLVM\bin"
)
if (
    $null -eq $x64Metadata -or
    $x64Metadata.MajorVersion -ne 22 -or
    $x64Metadata.Architecture -ne "X64"
) {
    throw "Could not parse realistic multiline x64 Clang output."
}
if (-not (Test-CompatibleClangToolchainMetadata `
    -ClangMetadata $x64Metadata `
    -ClangCxxMetadata $x64Metadata `
    -MinimumMajorVersion 20 `
    -HostArchitecture "X64"
)) {
    throw "A complete, compatible x64 Clang toolchain was rejected."
}

$arm64Metadata = ConvertFrom-ClangVersionOutput -VersionOutput @(
    "clang version 20.1.8"
    "Target: aarch64-pc-windows-msvc"
    "Thread model: posix"
    "InstalledDir: C:\Program Files\LLVM\bin"
)
if (
    $null -eq $arm64Metadata -or
    $arm64Metadata.MajorVersion -ne 20 -or
    $arm64Metadata.Architecture -ne "Arm64"
) {
    throw "Could not parse realistic multiline ARM64 Clang output."
}
if (Test-CompatibleClangToolchainMetadata `
    -ClangMetadata $x64Metadata `
    -ClangCxxMetadata $arm64Metadata `
    -MinimumMajorVersion 20 `
    -HostArchitecture "X64"
) {
    throw "A mixed-architecture Clang toolchain was accepted."
}

$olderX64Metadata = ConvertFrom-ClangVersionOutput -VersionOutput @(
    "clang version 19.1.7"
    "Target: x86_64-pc-windows-msvc"
)
if (Test-CompatibleClangToolchainMetadata `
    -ClangMetadata $x64Metadata `
    -ClangCxxMetadata $olderX64Metadata `
    -MinimumMajorVersion 20 `
    -HostArchitecture "X64"
) {
    throw "A mixed-version Clang toolchain was accepted."
}

$invalidMetadata = ConvertFrom-ClangVersionOutput -VersionOutput @(
    "clang version 22.1.8"
    "Target: wasm32-unknown-unknown"
)
if ($null -ne $invalidMetadata) {
    throw "Unsupported Clang target architecture was accepted."
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$vcpkgMetadataPath = Join-Path $projectRoot "metadata\VCPKG_TOOLCHAIN"
$vcpkgMetadata = @{}
foreach ($line in Get-Content -LiteralPath $vcpkgMetadataPath) {
    if ($line -notmatch '^([a-z_]+) ([^ ]+)$' -or $vcpkgMetadata.ContainsKey($Matches[1])) {
        throw "Invalid vcpkg metadata test fixture: $line"
    }
    $vcpkgMetadata[$Matches[1]] = $Matches[2]
}
$vcpkgManifest = Get-Content -LiteralPath (Join-Path $projectRoot "vcpkg.json") -Raw |
    ConvertFrom-Json
if (
    $vcpkgMetadata.repository -ne "https://github.com/microsoft/vcpkg.git" -or
    $vcpkgMetadata.release -ne "2026.04.27" -or
    $vcpkgMetadata.commit -ne "56bb2411609227288b70117ead2c47585ba07713" -or
    $vcpkgManifest.'builtin-baseline' -ne $vcpkgMetadata.commit -or
    (Compare-Object `
        @($vcpkgManifest.dependencies | Sort-Object) `
        @("freetype", "glfw3", "nlohmann-json"))
) {
    throw "The vcpkg toolchain and manifest do not match the pinned dependency baseline."
}
foreach ($architecture in @("x64", "arm64")) {
    $triplet = Get-Content -LiteralPath (
        Join-Path `
            $projectRoot `
            "cmake\vcpkg-triplets\$architecture-windows-static-crt.cmake"
    ) -Raw
    if (
        $triplet -notmatch "set\\(VCPKG_TARGET_ARCHITECTURE $architecture\\)" -or
        $triplet -notmatch "set\\(VCPKG_CRT_LINKAGE static\\)" -or
        $triplet -notmatch "set\\(VCPKG_LIBRARY_LINKAGE dynamic\\)" -or
        $triplet -notmatch "set\\(VCPKG_BUILD_TYPE release\\)"
    ) {
        throw "The $architecture Windows dependency triplet does not build release DLLs with a static CRT."
    }
}

$llvmMingwMetadata = Read-LlvmMingwMetadata -ProjectRoot $projectRoot
if (
    $llvmMingwMetadata.release -ne "20260616" -or
    $llvmMingwMetadata.llvm -ne "22" -or
    $llvmMingwMetadata.schema -ne "3" -or
    $llvmMingwMetadata.x64_sha256 -ne "b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35" -or
    $llvmMingwMetadata.arm64_sha256 -ne "312593669435bd0bfc1a43ac3fba23c8b27e0610bade88b2738e5a01702a99ba"
) {
    throw "LLVM MinGW metadata does not match the researched release assets."
}
$llvmMingwX64 = Get-LlvmMingwHost -HostArchitecture "X64"
$llvmMingwArm64 = Get-LlvmMingwHost -HostArchitecture "Arm64"
if (
    $llvmMingwX64.TargetTriple -ne "x86_64-w64-mingw32" -or
    $llvmMingwArm64.TargetTriple -ne "aarch64-w64-mingw32"
) {
    throw "LLVM MinGW host architecture mapping is invalid."
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) "dynlex-stage-test-$([guid]::NewGuid())"
try {
    $toolchainRoot = Join-Path $temporaryRoot "toolchain"
    $dependencyRoot = Join-Path $temporaryRoot "dependencies"
    $installationRoot = Join-Path $temporaryRoot "installation"
    foreach ($directory in @(
        (Join-Path $toolchainRoot "bin"),
        (Join-Path $toolchainRoot "lib\clang\22\lib\windows"),
        (Join-Path $toolchainRoot "lib\clang\22\include"),
        (Join-Path $toolchainRoot "include"),
        (Join-Path $toolchainRoot "include\sys"),
        (Join-Path $toolchainRoot "x86_64-w64-mingw32\lib"),
        (Join-Path $toolchainRoot "x86_64-w64-mingw32\share\mingw32"),
        (Join-Path $dependencyRoot "lib"),
        (Join-Path $dependencyRoot "bin"),
        (Join-Path $dependencyRoot "share\glfw3"),
        (Join-Path $dependencyRoot "share\freetype"),
        $installationRoot
    )) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    foreach ($relativePath in @(
        "LICENSE.TXT",
        "bin\cc.exe",
        "bin\clang-22.exe",
        "bin\mingw32-common.cfg",
        "bin\x86_64-w64-windows-gnu.cfg",
        "bin\llvm-ar.exe",
        "bin\ld.lld.exe",
        "bin\libLLVM-22.dll",
        "bin\libclang-cpp.dll",
        "bin\libc++.dll",
        "bin\libunwind.dll",
        "lib\clang\22\lib\windows\libclang_rt.builtins-x86_64.a",
        "lib\clang\22\include\stdbool.h",
        "lib\clang\22\include\stddef.h",
        "lib\clang\22\include\x86intrin.h",
        "include\windows.h",
        "include\winioctl.h",
        "include\shlobj.h",
        "include\sys\types.h",
        "x86_64-w64-mingw32\lib\crt2.o",
        "x86_64-w64-mingw32\lib\libkernel32.a",
        "x86_64-w64-mingw32\share\mingw32\COPYING"
    )) {
        Set-Content -Path (Join-Path $toolchainRoot $relativePath) -Value $relativePath
    }
    Assert-LlvmMingwLayout `
        -ToolchainRoot $toolchainRoot `
        -LlvmVersion "22" `
        -TargetArchitecture "x86_64"
    foreach ($relativePath in @(
        "lib\glfw3dll.lib",
        "lib\freetype.lib",
        "bin\glfw3.dll",
        "bin\freetype.dll",
        "bin\libpng16.dll",
        "share\glfw3\copyright",
        "share\freetype\copyright"
    )) {
        Set-Content -Path (Join-Path $dependencyRoot $relativePath) -Value $relativePath
    }

    & (Join-Path $PSScriptRoot "stage-windows-toolchain.ps1") `
        -InstallationRoot $installationRoot `
        -ToolchainRoot $toolchainRoot `
        -TargetArchitecture "x86_64" `
        -LlvmVersion "22" `
        -DependencyRoot $dependencyRoot
    foreach ($relativePath in @(
        "lib\dynlex\toolchain\bin\cc.exe",
        "lib\dynlex\toolchain\bin\mingw32-common.cfg",
        "lib\dynlex\toolchain\bin\x86_64-w64-windows-gnu.cfg",
        "lib\dynlex\toolchain\lib\clang\22\include\stdbool.h",
        "lib\dynlex\toolchain\lib\clang\22\include\stddef.h",
        "lib\dynlex\toolchain\lib\clang\22\include\x86intrin.h",
        "lib\dynlex\toolchain\x86_64-w64-mingw32\lib\libkernel32.a",
        "lib\dynlex\toolchain\dependencies\lib\libglfw3dll.a",
        "lib\dynlex\toolchain\dependencies\lib\libfreetype.a",
        "lib\dynlex\toolchain\runtime\glfw3.dll",
        "lib\dynlex\toolchain\runtime\freetype.dll",
        "lib\dynlex\toolchain\runtime\libpng16.dll",
        "lib\dynlex\toolchain\licenses\vcpkg\glfw3.txt",
        "lib\dynlex\toolchain\licenses\vcpkg\freetype.txt"
    )) {
        if (-not (Test-Path -PathType Leaf (Join-Path $installationRoot $relativePath))) {
            throw "Windows toolchain staging omitted $relativePath."
        }
    }
} finally {
    if (Test-Path $temporaryRoot) {
        Remove-Item -Recurse -Force $temporaryRoot
    }
}

Write-Host "Windows bootstrap and developer environment validation is valid."
