param(
    [Parameter(Mandatory=$true)][string]$InputPath,
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$source = [System.Drawing.Image]::FromFile($InputPath)
$bitmap = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($source, 0, 0, 256, 256)

    $rect = New-Object System.Drawing.Rectangle 0, 0, 256, 256
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = [Math]::Abs($data.Stride)
        $raw = New-Object byte[] ($stride * 256)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $raw, 0, $raw.Length)
    } finally {
        $bitmap.UnlockBits($data)
    }
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
    $source.Dispose()
}

# ICO: one 256x256 32-bit DIB image. DIB height includes the XOR and AND masks.
$pixelBytes = 256 * 256 * 4
$maskStride = ((256 + 31) / 32) * 4
$maskBytes = $maskStride * 256
$imageBytes = 40 + $pixelBytes + $maskBytes
$stream = [System.IO.File]::Create($OutputPath)
$writer = New-Object System.IO.BinaryWriter $stream
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]1)
    $writer.Write([Byte]0) # 256 is represented by zero in ICO directory
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$imageBytes)
    $writer.Write([UInt32]22)

    $writer.Write([UInt32]40) # BITMAPINFOHEADER
    $writer.Write([Int32]256)
    $writer.Write([Int32]512)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]0)
    $writer.Write([UInt32]$pixelBytes)
    $writer.Write([Int32]0)
    $writer.Write([Int32]0)
    $writer.Write([UInt32]0)
    $writer.Write([UInt32]0)

    # DIB pixel rows are stored bottom-up. GDI+ rows are copied from Scan0
    # using its actual stride, then written in reverse vertical order.
    for ($y = 255; $y -ge 0; $y--) {
        $rowOffset = $y * $stride
        $writer.Write($raw, $rowOffset, 256 * 4)
    }
    $writer.Write((New-Object byte[] $maskBytes))
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
