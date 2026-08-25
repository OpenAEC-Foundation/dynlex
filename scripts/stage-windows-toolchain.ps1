param(
    [Parameter(Mandatory = $true)]
    [string]$InstallationRoot,

    [Parameter(Mandatory = $true)]
    [string]$ToolchainRoot,

    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64", "aarch64")]
    [string]$TargetArchitecture,

    [Parameter(Mandatory = $true)]
    [string]$LlvmVersion,

    [Parameter(Mandatory = $true)]
    [string]$DependencyRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$targetTriple = "$TargetArchitecture-w64-mingw32"
$destinationRoot = Join-Path $InstallationRoot "lib\dynlex\toolchain"
$destinationBin = Join-Path $destinationRoot "bin"
$destinationResourceRoot = Join-Path $destinationRoot "lib\clang\$LlvmVersion"
$destinationResourceInclude = Join-Path $destinationResourceRoot "include"
$destinationResourceLibraries = Join-Path $destinationResourceRoot "lib\windows"
$destinationTarget = Join-Path $destinationRoot "$targetTriple"
$destinationTargetLib = Join-Path $destinationTarget "lib"
$destinationTargetLicenses = Join-Path $destinationTarget "share\mingw32"
$destinationDependencyLib = Join-Path $destinationRoot "dependencies\lib"
$destinationRuntime = Join-Path $destinationRoot "runtime"
$destinationVcpkgLicenses = Join-Path $destinationRoot "licenses\vcpkg"

if (Test-Path $destinationRoot) {
    throw "Windows toolchain destination already exists: $destinationRoot"
}
New-Item `
    -ItemType Directory `
    -Path $destinationBin, $destinationResourceInclude, $destinationResourceLibraries, $destinationTargetLib |
    Out-Null
New-Item -ItemType Directory -Path $destinationTargetLicenses, $destinationDependencyLib |
    Out-Null
New-Item -ItemType Directory -Path $destinationRuntime, $destinationVcpkgLicenses |
    Out-Null

foreach ($fileName in @(
    "cc.exe",
    "clang-$LlvmVersion.exe",
    "mingw32-common.cfg",
    "$TargetArchitecture-w64-windows-gnu.cfg",
    "ld.lld.exe",
    "libLLVM-$LlvmVersion.dll",
    "libclang-cpp.dll",
    "libc++.dll",
    "libunwind.dll"
)) {
    Copy-Item -Path (Join-Path $ToolchainRoot "bin\$fileName") -Destination $destinationBin
}
Copy-Item -Path (Join-Path $ToolchainRoot "LICENSE.TXT") -Destination $destinationRoot
Copy-Item `
    -Path (Join-Path $ToolchainRoot "lib\clang\$LlvmVersion\include\*") `
    -Destination $destinationResourceInclude `
    -Recurse
Copy-Item `
    -Path (Join-Path $ToolchainRoot "lib\clang\$LlvmVersion\lib\windows\libclang_rt.builtins-$TargetArchitecture.a") `
    -Destination $destinationResourceLibraries
Copy-Item -Path (Join-Path $ToolchainRoot "$targetTriple\lib\*") -Destination $destinationTargetLib
Copy-Item -Path (Join-Path $ToolchainRoot "$targetTriple\share\mingw32\*") -Destination $destinationTargetLicenses

$dependencyLibraryDirectory = Join-Path $DependencyRoot "lib"
$dependencyBinaryDirectory = Join-Path $DependencyRoot "bin"
$glfwImportLibraries = @(Get-ChildItem -Path $dependencyLibraryDirectory -Filter "*glfw3*dll.lib" -File)
$freetypeImportLibraries = @(Get-ChildItem -Path $dependencyLibraryDirectory -Filter "freetype.lib" -File)
if ($glfwImportLibraries.Count -ne 1 -or $freetypeImportLibraries.Count -ne 1) {
    throw "Expected exactly one dynamic GLFW and FreeType import library in $dependencyLibraryDirectory."
}
Copy-Item $glfwImportLibraries[0].FullName (Join-Path $destinationDependencyLib "libglfw3dll.a")
Copy-Item $freetypeImportLibraries[0].FullName (Join-Path $destinationDependencyLib "libfreetype.a")

$runtimeLibraries = @(Get-ChildItem -Path $dependencyBinaryDirectory -Filter "*.dll" -File)
if ($runtimeLibraries.Count -eq 0) {
    throw "No vcpkg runtime libraries were found in $dependencyBinaryDirectory."
}
Copy-Item -Path $runtimeLibraries.FullName -Destination $destinationRuntime

$shareDirectory = Join-Path $DependencyRoot "share"
$copyrightFiles = @(Get-ChildItem -Path $shareDirectory -Filter "copyright" -File -Recurse)
if ($copyrightFiles.Count -eq 0) {
    throw "No vcpkg dependency copyright files were found in $shareDirectory."
}
foreach ($copyrightFile in $copyrightFiles) {
    $packageName = Split-Path -Leaf (Split-Path -Parent $copyrightFile.FullName)
    Copy-Item $copyrightFile.FullName (Join-Path $destinationVcpkgLicenses "$packageName.txt")
}

Write-Host "Staged the self-contained Windows linker at $destinationRoot."
