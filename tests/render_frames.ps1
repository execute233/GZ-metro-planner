param([string]$Directory = 'cmake-build-debug/tests')
# Render exact CellSurface fixtures; these are frame previews, not terminal screenshots.
Add-Type -AssemblyName System.Drawing
$font = [System.Drawing.Font]::new('Cascadia Mono', 13, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$format = [System.Drawing.StringFormat]::GenericTypographic.Clone()
Get-ChildItem -LiteralPath $Directory -Filter '*.frame' | ForEach-Object {
    $lines = [System.IO.File]::ReadAllLines($_.FullName)
    $dimensions = $lines[0].Split(' ')
    $cols = [int]$dimensions[0]; $rows = [int]$dimensions[1]
    $bitmap = [System.Drawing.Bitmap]::new($cols * 9, $rows * 18)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Black)
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    for ($i = 0; $i -lt $cols * $rows; $i++) {
        $fields = $lines[$i + 1].Split(' ')
        if ($fields[2] -eq '1' -or $fields[0] -eq '0') { continue }
        $rgb = [int]$fields[1]
        $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, ($rgb -shr 16) -band 255, ($rgb -shr 8) -band 255, $rgb -band 255))
        $graphics.DrawString([char]::ConvertFromUtf32([int]$fields[0]), $font, $brush, [single](($i % $cols) * 9), [single]([math]::Floor($i / $cols) * 18), $format)
        $brush.Dispose()
    }
    $outputPath = [System.IO.Path]::ChangeExtension($_.FullName, '.png')
    $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    Write-Output $outputPath
}
$font.Dispose(); $format.Dispose()
