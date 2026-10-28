/* 光栅化算法，无平台依赖（tests/test_lcd_raster.c 在 PC 上逐像素比对）。
 * 把同一行/列上的连续像素合并成一段，减少 8080 屏最贵的开窗操作。 */
#include "lcd_raster.h"

static void raster_emit_h(lcd_run_cb_t cb, void *ctx, int32_t xa, int32_t xb, int32_t y)
{
  if (xa <= xb) cb(ctx, xa, y, xb - xa + 1, 0);
  else          cb(ctx, xb, y, xa - xb + 1, 0);
}

static void raster_emit_v(lcd_run_cb_t cb, void *ctx, int32_t x, int32_t ya, int32_t yb)
{
  if (ya <= yb) cb(ctx, x, ya, yb - ya + 1, 1);
  else          cb(ctx, x, yb, ya - yb + 1, 1);
}

void LCD_RasterLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    lcd_run_cb_t cb, void *ctx)
{
  int32_t dx, dy, ux, uy, err, x, y, run;

  if (cb == 0) return;

  if (y1 == y2) { raster_emit_h(cb, ctx, x1, x2, y1); return; }
  if (x1 == x2) { raster_emit_v(cb, ctx, x1, y1, y2); return; }

  dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
  dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
  ux = (x2 > x1) ? 1 : -1;
  uy = (y2 > y1) ? 1 : -1;
  x  = x1;
  y  = y1;

  if (dx >= dy)
  {
    run = x1;
    err = -dx / 2;
    while (x != x2)
    {
      x += ux;
      err += dy;
      if (err >= 0)
      {
        raster_emit_h(cb, ctx, run, x - ux, y);
        y += uy;
        err -= dx;
        run = x;
      }
    }
    raster_emit_h(cb, ctx, run, x2, y);
  }
  else
  {
    run = y1;
    err = -dy / 2;
    while (y != y2)
    {
      y += uy;
      err += dx;
      if (err >= 0)
      {
        raster_emit_v(cb, ctx, x, run, y - uy);
        x += ux;
        err -= dy;
        run = y;
      }
    }
    raster_emit_v(cb, ctx, x, run, y2);
  }
}
