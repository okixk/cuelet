[CmdletBinding()]
param(
    [string]$SourceDirectory,
    [string]$OutputPath,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
    $SourceDirectory = Join-Path $windowsRoot 'Cuelet.WinUI\Assets'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $windowsRoot 'Cuelet.WinUI\Assets\Cuelet.ico'
}
$frames = @(
    @{ Size = 16; Name = 'Square44x44Logo.altform-unplated_targetsize-16.png' },
    @{ Size = 24; Name = 'Square44x44Logo.targetsize-24_altform-unplated.png' },
    @{ Size = 32; Name = 'Square44x44Logo.altform-unplated_targetsize-32.png' },
    @{ Size = 48; Name = 'Square44x44Logo.altform-unplated_targetsize-48.png' },
    @{ Size = 256; Name = 'Square44x44Logo.altform-unplated_targetsize-256.png' }
)

function Read-BigEndianUInt32([byte[]]$Bytes, [int]$Offset) {
    return ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3]
}

$images = foreach ($frame in $frames) {
    $path = Join-Path $SourceDirectory $frame.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required icon frame is missing: $path"
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 24 -or
        $bytes[0] -ne 0x89 -or $bytes[1] -ne 0x50 -or
        $bytes[2] -ne 0x4e -or $bytes[3] -ne 0x47) {
        throw "Icon frame is not a valid PNG: $path"
    }
    $width = Read-BigEndianUInt32 $bytes 16
    $height = Read-BigEndianUInt32 $bytes 20
    if ($width -ne $frame.Size -or $height -ne $frame.Size) {
        throw "Icon frame $path is ${width}x${height}; expected $($frame.Size)x$($frame.Size)."
    }
    [PSCustomObject]@{ Size = [int]$frame.Size; Bytes = $bytes }
}

$temporary = [IO.Path]::GetTempFileName()
try {
    $stream = [IO.File]::Open($temporary, [IO.FileMode]::Create, [IO.FileAccess]::Write)
    try {
        $writer = [IO.BinaryWriter]::new($stream)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]$images.Count)
            $offset = 6 + (16 * $images.Count)
            foreach ($image in $images) {
                $dimension = if ($image.Size -eq 256) { 0 } else { $image.Size }
                $writer.Write([byte]$dimension)
                $writer.Write([byte]$dimension)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]$image.Bytes.Length)
                $writer.Write([uint32]$offset)
                $offset += $image.Bytes.Length
            }
            foreach ($image in $images) {
                $writer.Write($image.Bytes)
            }
        } finally {
            $writer.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    if ($Check) {
        if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash) {
            throw "Generated Windows icon is stale: $OutputPath"
        }
        Write-Host "Windows icon is current: $OutputPath"
    } else {
        [IO.File]::Copy($temporary, $OutputPath, $true)
        Write-Host "Generated Windows icon: $OutputPath"
    }
} finally {
    [IO.File]::Delete($temporary)
}
