/* DMA 异步刷屏（MDMA）。LCD_USE_DMA=0 时全部退化为同步 LCD_BlitArea。
 * 源缓冲必须在 DMA 可达的 RAM（AXI SRAM / D2 SRAM），**不能是 DTCM**。 */
#ifndef __LCD_DMA_H
#define __LCD_DMA_H

#include "lcd_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

void    LCD_DMA_Init(void);
void    LCD_DMA_BlitStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const uint16_t *src, uint16_t pitch);
uint8_t LCD_DMA_IsBusy(void);
void    LCD_DMA_Wait(void);
uint8_t LCD_DMA_GetError(void);   /* 1 = 传输出错(或源在 DTCM)，已自动退回同步 */

#ifdef __cplusplus
}
#endif

#endif /* __LCD_DMA_H */
