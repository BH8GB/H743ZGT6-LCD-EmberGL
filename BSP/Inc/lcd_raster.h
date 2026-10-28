/* 与硬件无关的光栅化算法（PC 上可单元测试，见 tests/） */
#ifndef __LCD_RASTER_H
#define __LCD_RASTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一段同一行/列上连续的像素 */
typedef void (*lcd_run_cb_t)(void *ctx, int32_t x, int32_t y, int32_t len, int vertical);

/* 线段 → 连续段回调。输出像素与逐点 Bresenham 完全一致。 */
void LCD_RasterLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    lcd_run_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_RASTER_H */
