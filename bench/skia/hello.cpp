// bench/skia: a red circle on a 64x64 raster canvas, pixels checked (the link test: vpp -c, vlink
// against out/vcc/libskia.a; it links once vcc builds code for SkCanvas, see bench/skia/build.sh)
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include <cstdio>
int main()
{
    SkBitmap bm;
    bm.allocN32Pixels(64, 64);
    SkCanvas canvas(bm);
    canvas.clear(SK_ColorWHITE);
    SkPaint paint;
    paint.setColor(SK_ColorRED);
    paint.setAntiAlias(true);
    canvas.drawCircle(32, 32, 20, paint);
    SkColor mid = bm.getColor(32, 32), corner = bm.getColor(1, 1);
    unsigned red = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            red += bm.getColor(x, y) == SK_ColorRED;
    std::printf("center %08x corner %08x red pixels %u\n", mid, corner, red);
    return mid == SK_ColorRED && corner == SK_ColorWHITE && red > 1100 && red < 1300 ? 0 : 1;
}
