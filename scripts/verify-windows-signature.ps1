param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $ExpectedSubject = "Impertio Studio B.V."
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedSubjectPattern = [regex]::Escape($ExpectedSubject)

foreach ($inputPath in $Path) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
        throw "Signed artifact does not exist: $inputPath"
    }

    $resolvedPath = (Resolve-Path -LiteralPath $inputPath).Path
    $signature = Get-AuthenticodeSignature -FilePath $resolvedPath

    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode signature for '$resolvedPath' is $($signature.Status): $($signature.StatusMessage)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Authenticode signature for '$resolvedPath' has no signer certificate."
    }
    if ($signature.SignerCertificate.Subject -notmatch $expectedSubjectPattern) {
        throw "Authenticode signer for '$resolvedPath' is '$($signature.SignerCertificate.Subject)', expected '$ExpectedSubject'."
    }
    if ($null -eq $signature.TimeStamperCertificate) {
        throw "Authenticode signature for '$resolvedPath' has no trusted timestamp."
    }

    Write-Host "Verified Authenticode signature and timestamp for '$resolvedPath'."
}
