/* 分块画布：给软件渲染器（EmberGL/TinyGL 等）用的连续 RGB565 缓冲。
 * 整屏 800x480x2=768KB 放不下内部 RAM，所以按横带渲染 + 整块推屏。 */
#ifndef __LCD_CANVAS_H
#define __LCD_CANVAS_H

#include "lcd_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LCD_USE_CANVAS

void      LCD_CanvasInit(uint16_t lines);   /* 0 = 用 LCD_CANVAS_LINES */
uint16_t *LCD_CanvasBuf(void);              /* 行距 = LCD_CanvasWidth() */
uint16_t  LCD_CanvasWidth(void);
uint16_t  LCD_CanvasHeight(void);
void      LCD_CanvasPush(uint16_t x, uint16_t y);
void      LCD_CanvasPushRows(uint16_t x, uint16_t y, uint16_t rows);
void      LCD_CanvasPushAsync(uint16_t x, uint16_t y);   /* 需 LCD_USE_DMA */
uint8_t   LCD_CanvasBusy(void);
void      LCD_CanvasWait(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __LCD_CANVAS_H */
