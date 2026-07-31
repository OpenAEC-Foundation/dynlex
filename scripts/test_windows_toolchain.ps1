Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "windows-toolchain.ps1")

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

Write-Host "Windows bootstrap Clang metadata parsing is valid."
