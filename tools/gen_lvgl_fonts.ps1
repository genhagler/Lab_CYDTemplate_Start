# =====================================================================
#  gen_lvgl_fonts.ps1 -- สร้างฟอนต์ภาษาไทยสำหรับ LVGL 8.3 (ไฟล์ .c ใน src/fonts/)
#
#  ใช้ lv_font_conv (เครื่องมือทางการของ LVGL, ต้องมี Node.js) รวม 3 ส่วนในฟอนต์เดียว:
#    1) ASCII + องศา (°)                 จาก Chakra Petch
#    2) อักษรไทย U+0E01-U+0E5B            จาก Chakra Petch
#    3) สัญลักษณ์ LV_SYMBOL_* ของ LVGL    จาก FontAwesome (ไฟล์เดียวกับที่ LVGL ใช้สร้าง Montserrat)
#
#  วิธีใช้ (เปิด PowerShell ที่โฟลเดอร์โปรเจกต์):
#    powershell -ExecutionPolicy Bypass -File tools\gen_lvgl_fonts.ps1
#  เพิ่มขนาดฟอนต์: แก้ตาราง $fonts ด้านล่าง แล้วประกาศ LV_FONT_DECLARE(...) ใน src/ui.h
# =====================================================================

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$fontDir = Join-Path $PSScriptRoot "fonts"
$outDir = Join-Path $root "src\fonts"
New-Item -ItemType Directory -Force $outDir | Out-Null

# สัญลักษณ์ชุดเดียวกับฟอนต์ Montserrat ในตัว LVGL 8.3 (LV_SYMBOL_OK, LV_SYMBOL_PLAY, ...)
$symbols = "61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

# ชื่อไฟล์ = ชื่อตัวแปรฟอนต์ใน C, ขนาด (px), ไฟล์ TTF
$fonts = @(
    @{ name = "font_th_16"; size = 16; ttf = "ChakraPetch-Regular.ttf" },
    @{ name = "font_th_24"; size = 24; ttf = "ChakraPetch-SemiBold.ttf" }
)

foreach ($f in $fonts) {
    $out = Join-Path $outDir ($f.name + ".c")
    Write-Host "==> $($f.name) ($($f.size) px) -> $out"
    npx -y lv_font_conv@1.5.2 --no-compress --no-prefilter --bpp 4 --size $f.size --format lvgl `
        --lv-include lvgl.h `
        --font (Join-Path $fontDir $f.ttf) -r "0x20-0x7E,0xB0,0x0E01-0x0E5B" `
        --font (Join-Path $fontDir "FontAwesome5-Solid+Brands+Regular.woff") -r $symbols `
        -o $out
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $($f.name)" }
}
Write-Host "done"
