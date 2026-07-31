param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string] $Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$onlyDetectAttribute = 2
$minimumInclusiveAttribute = 256
$maximumInclusiveAttribute = 512
$upgradeCodeMetadataPath = Join-Path `
    (Split-Path -Parent $PSScriptRoot) `
    "metadata\WINDOWS_INSTALLER_UPGRADE_CODES"

if (-not (Test-Path -LiteralPath $upgradeCodeMetadataPath -PathType Leaf)) {
    throw "Windows installer upgrade code metadata does not exist: $upgradeCodeMetadataPath"
}

$upgradeCodes = @{}
foreach ($line in Get-Content -LiteralPath $upgradeCodeMetadataPath) {
    if ($line -notmatch '^([a-z]+) ([A-F0-9]{8}(?:-[A-F0-9]{4}){3}-[A-F0-9]{12})$') {
        throw "Invalid Windows installer upgrade code metadata: $line"
    }
    if ($upgradeCodes.ContainsKey($Matches[1])) {
        throw "Duplicate Windows installer upgrade code metadata: $($Matches[1])"
    }
    $upgradeCodes[$Matches[1]] = $Matches[2]
}
if ($upgradeCodes.Count -ne 2 -or
    -not $upgradeCodes.ContainsKey("stable") -or
    -not $upgradeCodes.ContainsKey("legacy")) {
    throw "Windows installer upgrade code metadata must define stable and legacy codes."
}

$stableUpgradeCode = $upgradeCodes["stable"]
$legacyUpgradeCode = $upgradeCodes["legacy"]

function Invoke-ComMethod {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Object,

        [Parameter(Mandatory = $true)]
        [string] $Name,

        [object[]] $Arguments = @()
    )

    return $Object.GetType().InvokeMember(
        $Name,
        [System.Reflection.BindingFlags]::InvokeMethod,
        $null,
        $Object,
        $Arguments
    )
}

function Get-ComProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Object,

        [Parameter(Mandatory = $true)]
        [string] $Name,

        [object[]] $Arguments = @()
    )

    return $Object.GetType().InvokeMember(
        $Name,
        [System.Reflection.BindingFlags]::GetProperty,
        $null,
        $Object,
        $Arguments
    )
}

function Normalize-UpgradeCode {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Value
    )

    return $Value.Trim().TrimStart("{").TrimEnd("}").ToUpperInvariant()
}

function Read-SingleString {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Database,

        [Parameter(Mandatory = $true)]
        [string] $Query
    )

    $view = Invoke-ComMethod -Object $Database -Name "OpenView" -Arguments @($Query)
    try {
        Invoke-ComMethod -Object $view -Name "Execute" | Out-Null
        $record = Invoke-ComMethod -Object $view -Name "Fetch"
        if ($null -eq $record) {
            throw "MSI query returned no records: $Query"
        }
        try {
            $value = [string](Get-ComProperty -Object $record -Name "StringData" -Arguments @(1))
        } finally {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
        }

        $extraRecord = Invoke-ComMethod -Object $view -Name "Fetch"
        if ($null -ne $extraRecord) {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($extraRecord)
            throw "MSI query returned multiple records: $Query"
        }
        return $value
    } finally {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view)
    }
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Windows installer does not exist: $Path"
}

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $null
try {
    $database = Invoke-ComMethod -Object $installer -Name "OpenDatabase" -Arguments @(
        $resolvedPath,
        0
    )
    $productUpgradeCode = Normalize-UpgradeCode (Read-SingleString `
        -Database $database `
        -Query "SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = 'UpgradeCode'")
    if ($productUpgradeCode -ne $stableUpgradeCode) {
        throw "MSI UpgradeCode is '$productUpgradeCode', expected '$stableUpgradeCode'."
    }

    $productVersion = Read-SingleString `
        -Database $database `
        -Query "SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = 'ProductVersion'"
    $view = Invoke-ComMethod `
        -Object $database `
        -Name "OpenView" `
        -Arguments @(
            "SELECT ``UpgradeCode``, ``VersionMin``, ``VersionMax``, ``Attributes``, ``ActionProperty`` FROM ``Upgrade``"
        )
    $rows = @()
    try {
        Invoke-ComMethod -Object $view -Name "Execute" | Out-Null
        while ($null -ne ($record = Invoke-ComMethod -Object $view -Name "Fetch")) {
            try {
                $rows += [PSCustomObject]@{
                    UpgradeCode = Normalize-UpgradeCode ([string](
                        Get-ComProperty -Object $record -Name "StringData" -Arguments @(1)
                    ))
                    VersionMin = [string](
                        Get-ComProperty -Object $record -Name "StringData" -Arguments @(2)
                    )
                    VersionMax = [string](
                        Get-ComProperty -Object $record -Name "StringData" -Arguments @(3)
                    )
                    Attributes = [int](
                        Get-ComProperty -Object $record -Name "IntegerData" -Arguments @(4)
                    )
                    ActionProperty = [string](
                        Get-ComProperty -Object $record -Name "StringData" -Arguments @(5)
                    )
                }
            } finally {
                [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
            }
        }
    } finally {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view)
    }

    if (-not ($rows | Where-Object { $_.UpgradeCode -eq $stableUpgradeCode })) {
        throw "MSI does not contain its stable upgrade family in the Upgrade table."
    }

    $legacyRows = @($rows | Where-Object {
        $_.UpgradeCode -eq $legacyUpgradeCode -and
        ($_.Attributes -band $onlyDetectAttribute) -eq 0
    })
    if ($legacyRows.Count -ne 1) {
        throw "MSI must contain exactly one removable legacy upgrade-family row."
    }
    $legacyRow = $legacyRows[0]
    if (
        $legacyRow.VersionMin -ne "0.0.0" -or
        ($legacyRow.Attributes -band $minimumInclusiveAttribute) -eq 0 -or
        $legacyRow.VersionMax -ne $productVersion -or
        ($legacyRow.Attributes -band $maximumInclusiveAttribute) -ne 0 -or
        $legacyRow.ActionProperty -ne "DYNLEX_LEGACY_VERSION_FOUND"
    ) {
        throw "MSI legacy upgrade-family row has unexpected version or action metadata."
    }

    $removeExistingProductsSequence = [int](Read-SingleString `
        -Database $database `
        -Query "SELECT ``Sequence`` FROM ``InstallExecuteSequence`` WHERE ``Action`` = 'RemoveExistingProducts'")
    if ($removeExistingProductsSequence -le 0) {
        throw "MSI RemoveExistingProducts action has an invalid sequence."
    }

    Write-Host "Verified stable and published legacy MSI upgrade families for '$resolvedPath'."
} finally {
    if ($null -ne $database) {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($database)
    }
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer)
}
