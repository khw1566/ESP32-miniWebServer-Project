# Regenerate the UI Hangul subset. ASCII uses LVGL Montserrat.
param([string]$FontName='Malgun Gothic')
Add-Type -AssemblyName System.Drawing
$root=Split-Path $PSScriptRoot -Parent
$source=(Get-Content -Raw -Encoding utf8 (Join-Path $root 'main/onboard_ui.c')) + (Get-Content -Raw -Encoding utf8 (Join-Path $root 'main/m1_keyboard.c')) + (Get-Content -Raw -Encoding utf8 (Join-Path $root 'main/m1_dashboard.c'))
# Hangul plus any other non-ASCII symbol (for example the middle dot); Montserrat covers ASCII only.
$codes=@($source.ToCharArray() | Where-Object {$c=[int]$_;($c -ge 0xAC00 -and $c -le 0xD7A3) -or ($c -gt 0x7E -and $c -lt 0xD800 -and -not [char]::IsWhiteSpace($_))} | ForEach-Object {[int]$_} | Sort-Object -Unique)
$image=[System.Drawing.Bitmap]::new(16,20)
$graphics=[System.Drawing.Graphics]::FromImage($image)
$graphics.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$font=[System.Drawing.Font]::new($FontName,14,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
$format=[System.Drawing.StringFormat]::GenericTypographic.Clone()
$out=[System.Text.StringBuilder]::new()
[void]$out.AppendLine('/* Generated UI Hangul subset. See tools/generate_font.ps1. */')
[void]$out.AppendLine('#include "board_display.h"')
[void]$out.AppendLine('static const uint32_t codes[] = {'+($codes -join ',')+'};')
[void]$out.AppendLine('static const uint8_t glyphs[][40] = {')
foreach($code in $codes) {
    $graphics.Clear([System.Drawing.Color]::White)
    $graphics.DrawString([string][char]$code,$font,[System.Drawing.Brushes]::Black,0,0,$format)
    $bytes=[System.Collections.Generic.List[int]]::new()
    for($y=0;$y -lt 20;$y++) {
        for($byte=0;$byte -lt 2;$byte++) {
            $value=0
            for($bit=0;$bit -lt 8;$bit++) {
                if($image.GetPixel($byte*8+$bit,$y).R -lt 128) {$value=$value -bor (1 -shl (7-$bit))}
            }
            $bytes.Add($value)
        }
    }
    [void]$out.AppendLine('{'+($bytes -join ',')+'},')
}
[void]$out.AppendLine('};')
[void]$out.AppendLine(@'
static int find(uint32_t code) {
    for(unsigned i=0;i<sizeof(codes)/sizeof(codes[0]);++i) if(codes[i]==code) return (int)i;
    return -1;
}
static bool descriptor(const lv_font_t *font,lv_font_glyph_dsc_t *d,uint32_t code,uint32_t next) {
    if(find(code)<0) return false;
    d->adv_w=16;d->box_w=16;d->box_h=20;d->ofs_x=0;d->ofs_y=-4;d->bpp=1;
    return true;
}
static const uint8_t *bitmap_for(const lv_font_t *font,uint32_t code) {
    int i=find(code);return i<0?NULL:glyphs[i];
}
const lv_font_t font_m1={.get_glyph_dsc=descriptor,.get_glyph_bitmap=bitmap_for,
    .line_height=20,.base_line=4,.fallback=&lv_font_montserrat_14};
'@)
[System.IO.File]::WriteAllText((Join-Path $root 'main/font_m1.c'),$out.ToString(),[System.Text.UTF8Encoding]::new($false))
$format.Dispose();$font.Dispose();$graphics.Dispose();$image.Dispose()
Write-Output "Generated $($codes.Count) Hangul glyphs"
