[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]*$')]
    [string] $Name,
    [switch] $Force,
    [string] $OutputRoot
)

$ErrorActionPreference = 'Stop'
$docsRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $docsRoot 'screenshots' }
$safeName = $Name.ToLowerInvariant()
$outputPath = Join-Path $OutputRoot ($safeName + '.png')
if ((Test-Path -LiteralPath $outputPath) -and -not $Force) {
    throw "Refusing to overwrite existing capture '$outputPath'. Use -Force explicitly."
}

$cuelet = Get-Process -Name Cuelet -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } |
    Sort-Object StartTime -Descending | Select-Object -First 1
if (-not $cuelet) { throw 'A visible top-level Cuelet window could not be identified safely.' }

$captureType = @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class CueletScreenCopy {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static string Capture(IntPtr handle, string path) {
        RECT rect;
        if (!IsWindowVisible(handle) || !GetWindowRect(handle, out rect)) throw new InvalidOperationException("Cuelet window is not visible.");
        int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
        if (width < 120 || height < 80 || width > 10000 || height > 10000) throw new InvalidOperationException("Cuelet window rectangle is invalid.");
        using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb)) {
            using (var graphics = Graphics.FromImage(bitmap)) {
                graphics.CopyFromScreen(rect.Left, rect.Top, 0, 0, new Size(width, height), CopyPixelOperation.SourceCopy);
            }
            long nonBlack = 0, sampled = 0;
            for (int y = 0; y < height; y += Math.Max(1, height / 80)) for (int x = 0; x < width; x += Math.Max(1, width / 120)) {
                var pixel = bitmap.GetPixel(x, y); sampled++;
                if (pixel.R > 10 || pixel.G > 10 || pixel.B > 10) nonBlack++;
            }
            if (sampled == 0 || ((double)nonBlack / sampled) < 0.01) throw new InvalidOperationException("Capture is almost completely black or empty; refusing to save it.");
            bitmap.Save(path, ImageFormat.Png);
        }
        return width + "x" + height;
    }
}
'@
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [System.Drawing.Bitmap].Assembly.Location
Add-Type -TypeDefinition $captureType -ReferencedAssemblies $drawingAssembly
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$dimensions = [CueletScreenCopy]::Capture($cuelet.MainWindowHandle, [IO.Path]::GetFullPath($outputPath))
$file = Get-Item -LiteralPath $outputPath
Write-Output ("Captured {0} ({1}, {2} bytes)" -f $file.FullName, $dimensions, $file.Length)
