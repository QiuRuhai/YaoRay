param(
    [Parameter(Mandatory = $true)]
    [string]$YaoRayExe,

    [Parameter(Mandatory = $true)]
    [string]$ScenePath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Remove-Item -Force -ErrorAction SilentlyContinue $OutputPath

$renderOutput = & $YaoRayExe render $ScenePath --backend cpu 2>&1 | Out-String
Write-Output $renderOutput
if ($LASTEXITCODE -ne 0) {
    exit 1
}
if ($renderOutput -notmatch "Integrator: path") {
    Write-Error "glass showcase did not use the path integrator"
    exit 1
}
if ($renderOutput -notmatch "Rendered image:") {
    Write-Error "glass showcase did not report a rendered image"
    exit 1
}
if (-not (Test-Path $OutputPath)) {
    Write-Error "glass showcase output was not written"
    exit 1
}

[byte[]]$bytes = [System.IO.File]::ReadAllBytes($OutputPath)
[byte[]]$expected = 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
if ($bytes.Length -lt 8) {
    Write-Error "glass showcase output is too short to be a PNG"
    exit 1
}
for ($i = 0; $i -lt 8; $i++) {
    if ($bytes[$i] -ne $expected[$i]) {
        Write-Error "glass showcase output does not have a PNG header"
        exit 1
    }
}

Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($OutputPath)
try {
    $nearWhite = 0
    $dark = 0
    $total = $bitmap.Width * $bitmap.Height
    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            if ([Math]::Min($pixel.R, [Math]::Min($pixel.G, $pixel.B)) -ge 245) {
                $nearWhite++
            }
            if ([Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)) -le 50) {
                $dark++
            }
        }
    }
    $nearWhiteFraction = $nearWhite / [double]$total
    $darkFraction = $dark / [double]$total
    Write-Output ("Glass visual sanity: near_white={0:N4} dark={1:N4}" -f $nearWhiteFraction, $darkFraction)
    if ($nearWhiteFraction -gt 0.70) {
        Write-Error "glass showcase is overexposed: too many near-white pixels"
        exit 1
    }
    if ($darkFraction -lt 0.03) {
        Write-Error "glass showcase has too little dark contrast for refraction to read"
        exit 1
    }
}
finally {
    $bitmap.Dispose()
}
