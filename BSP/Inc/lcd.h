/* ILI9806G 800x480 液晶驱动（FMC 8080 并口，LL 库） */
#ifndef __LCD_H
#define __LCD_H

#include "lcd_conf.h"
#include "lcd_bus.h"
#include "lcd_raster.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WHITE       0xFFFFU
#define BLACK       0x0000U
#define BLUE        0x001FU
#define RED         0xF800U
#define GREEN       0x07E0U
#define CYAN        0x07FFU
#define MAGENTA     0xF81FU
#define YELLOW      0xFFE0U
#define GRAY        0x8430U
#define BROWN       0xBC40U
#define BRRED       0xFC07U
#define DARKBLUE    0x01CFU
#define LIGHTBLUE   0x7D7CU
#define GRAYBLUE    0x5458U
#define LIGHTGREEN  0x841FU
#define LGRAY       0xC618U

typedef struct
{
  uint16_t width;   /* 当前方向下的宽高 */
  uint16_t height;
  uint16_t id;      /* 控制器 ID，ILI9806G = 0x9806 */
  uint8_t  dir;
  uint8_t  bgr;
} lcd_dev_t;

extern lcd_dev_t lcddev;

void     LCD_Init(void);
uint16_t LCD_ReadID(void);
void     LCD_DisplayDir(uint8_t dir);
void     LCD_Backlight(uint8_t on);
void     LCD_DisplayOn(void);
void     LCD_DisplayOff(void);

void     LCD_SetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
void     LCD_SetCursor(uint16_t x, uint16_t y);
void     LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);
void     LCD_Fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color);
void     LCD_Clear(uint16_t color);
void     LCD_ColorFill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const uint16_t *buf);

/* 图形库适配用的通用原语 */
void     LCD_WritePixels(const uint16_t *buf, uint32_t n);
void     LCD_WritePixelRepeat(uint16_t color, uint32_t n);
void     LCD_DrawSpan(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void     LCD_BlitSpan(uint16_t x, uint16_t y, uint16_t len, const uint16_t *src);
void     LCD_BlitArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *src, uint16_t pitch);   /* pitch==w 时最快 */
uint16_t LCD_GetWidth(void);
uint16_t LCD_GetHeight(void);

void     LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawHLine(uint16_t x1, uint16_t x2, uint16_t y, uint16_t color);
void     LCD_DrawVLine(uint16_t x, uint16_t y1, uint16_t y2, uint16_t color);
void     LCD_DrawRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

#if LCD_USE_TEXT
/* size 支持 16(8x16) / 24(12x24) / 32(16x32) */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size, uint16_t fc, uint16_t bc);
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t size, uint16_t fc, uint16_t bc);
void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size,
                 uint16_t fc, uint16_t bc);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
