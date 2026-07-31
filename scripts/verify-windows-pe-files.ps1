param(
    [Parameter(Mandatory = $true)]
    [string[]]$Files,

    [Parameter(Mandatory = $true)]
    [ValidateSet("x64", "arm64")]
    [string]$Architecture,

    [string]$PythonCommand = "python"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Files.Count -eq 0) {
    throw "No Windows PE files were provided for verification."
}

foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Windows PE file does not exist: $file"
    }
    & $PythonCommand `
        (Join-Path $PSScriptRoot "verify-executable-architecture.py") `
        $file `
        $Architecture
    if ($LASTEXITCODE -ne 0) {
        throw "Windows PE file has the wrong architecture: $file"
    }
    & $PythonCommand `
        (Join-Path $PSScriptRoot "verify-windows-runtime-dependencies.py") `
        $file
    if ($LASTEXITCODE -ne 0) {
        throw "Windows PE file dynamically depends on the Visual C++ runtime: $file"
    }
}

Write-Host "Verified $($Files.Count) Windows PE file(s) for $Architecture."
