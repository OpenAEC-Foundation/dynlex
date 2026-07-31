Set-StrictMode -Version Latest

function ConvertFrom-WindowsCommandEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Lines
    )

    $environment = @{}
    foreach ($line in $Lines) {
        $separator = $line.IndexOf("=")
        if ($separator -le 0) {
            continue
        }
        $name = $line.Substring(0, $separator)
        if ($environment.ContainsKey($name)) {
            throw "Windows command environment contains duplicate variable '$name'."
        }
        $environment[$name] = $line.Substring($separator + 1)
    }
    return $environment
}

function ConvertFrom-ClangVersionOutput {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $VersionOutput
    )

    $versionText = $VersionOutput -join "`n"
    if ($versionText -notmatch '(?m)^clang version ([0-9]+)(?:\.[0-9.]+)?(?:\s|$)') {
        return $null
    }
    $majorVersion = [int]$Matches[1]

    if ($versionText -notmatch '(?m)^Target:\s+([^- \r\n]+)') {
        return $null
    }
    $architecture = switch -Regex ($Matches[1].ToLowerInvariant()) {
        '^(x86_64|amd64)$' { "X64"; break }
        '^(i[3-6]86|x86)$' { "X86"; break }
        '^(aarch64|arm64)$' { "Arm64"; break }
        default { return $null }
    }

    return [PSCustomObject]@{
        MajorVersion = $majorVersion
        Architecture = $architecture
    }
}

function Get-ClangToolMetadata {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ExecutablePath
    )

    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        return $null
    }
    $versionOutput = & $ExecutablePath --version
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return ConvertFrom-ClangVersionOutput -VersionOutput $versionOutput
}

function Test-CompatibleClangToolchainMetadata {
    param(
        [object] $ClangMetadata,
        [object] $ClangCxxMetadata,
        [int] $MinimumMajorVersion,
        [string] $HostArchitecture
    )

    return (
        $null -ne $ClangMetadata -and
        $null -ne $ClangCxxMetadata -and
        $ClangMetadata.MajorVersion -ge $MinimumMajorVersion -and
        $ClangMetadata.MajorVersion -eq $ClangCxxMetadata.MajorVersion -and
        $ClangMetadata.Architecture -eq $HostArchitecture -and
        $ClangCxxMetadata.Architecture -eq $HostArchitecture
    )
}

function Resolve-CompatibleClangDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [int] $MinimumMajorVersion,

        [Parameter(Mandatory = $true)]
        [ValidateSet("X64", "X86", "Arm64")]
        [string] $HostArchitecture
    )

    $candidateDirectories = @()
    foreach (
        $clangCommand in @(
            Get-Command clang.exe -All -CommandType Application `
                -ErrorAction SilentlyContinue
        )
    ) {
        $candidateDirectories += Split-Path -Parent $clangCommand.Source
    }
    $candidateDirectories += Join-Path ${env:ProgramFiles} "LLVM\bin"

    foreach ($candidateDirectory in @($candidateDirectories | Select-Object -Unique)) {
        $clangMetadata = Get-ClangToolMetadata `
            -ExecutablePath (Join-Path $candidateDirectory "clang.exe")
        $clangCxxMetadata = Get-ClangToolMetadata `
            -ExecutablePath (Join-Path $candidateDirectory "clang++.exe")
        if (Test-CompatibleClangToolchainMetadata `
            -ClangMetadata $clangMetadata `
            -ClangCxxMetadata $clangCxxMetadata `
            -MinimumMajorVersion $MinimumMajorVersion `
            -HostArchitecture $HostArchitecture
        ) {
            return $candidateDirectory
        }
    }
    return $null
}
