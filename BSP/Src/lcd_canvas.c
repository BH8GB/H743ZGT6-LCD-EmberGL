/* 分块画布实现。缓冲位置由 lcd_conf.h 的 LCD_BUF_ATTR 决定（默认 AXI SRAM）。 */
#include "lcd_conf.h"

#if LCD_USE_CANVAS

#include "lcd_canvas.h"
#include "lcd.h"
#include "lcd_dma.h"

#define LCD_CANVAS_MAX_W   ((uint16_t)((LCD_PANEL_PORTRAIT_H > LCD_PANEL_PORTRAIT_W) ? \
                                       LCD_PANEL_PORTRAIT_H : LCD_PANEL_PORTRAIT_W))

/* 画布放在 AXI SRAM(0x2400_0000) 固定首地址：MDMA/DMA 可达，且不依赖会被
   CubeMX 重写的链接脚本。总占用 = 800 x 32 x 2 = 50 KB，AXI SRAM 共 512 KB。 */
#define LCD_CANVAS_BASE   (0x24000000UL)
#define LCD_CANVAS_WORDS  (LCD_CANVAS_MAX_W * LCD_CANVAS_LINES)

static uint16_t *const lcd_canvas_buf =
    (uint16_t *)LCD_CANVAS_BASE;

static uint16_t lcd_canvas_w     = LCD_CANVAS_MAX_W;
static uint16_t lcd_canvas_lines = LCD_CANVAS_LINES;

void LCD_CanvasInit(uint16_t lines)
{
  lcd_canvas_w = lcddev.width;

  if (lines == 0U) lines = LCD_CANVAS_LINES;
  if (lines > LCD_CANVAS_LINES) lines = LCD_CANVAS_LINES;
  lcd_canvas_lines = lines;

  LCD_DMA_Init();
}

uint16_t *LCD_CanvasBuf(void)      { return lcd_canvas_buf; }
uint16_t  LCD_CanvasWidth(void)    { return lcd_canvas_w; }
uint16_t  LCD_CanvasHeight(void)   { return lcd_canvas_lines; }

void LCD_CanvasPushRows(uint16_t x, uint16_t y, uint16_t rows)
{
  if (rows == 0U) return;
  if (rows > lcd_canvas_lines) rows = lcd_canvas_lines;

  LCD_BlitArea(x, y, lcd_canvas_w, rows, lcd_canvas_buf, lcd_canvas_w);
}

void LCD_CanvasPush(uint16_t x, uint16_t y)
{
  LCD_CanvasPushRows(x, y, lcd_canvas_lines);
}

void LCD_CanvasPushAsync(uint16_t x, uint16_t y)
{
#if LCD_USE_DMA
  LCD_DMA_BlitStart(x, y, lcd_canvas_w, lcd_canvas_lines, lcd_canvas_buf, lcd_canvas_w);
#else
  LCD_CanvasPush(x, y);
#endif
}

uint8_t LCD_CanvasBusy(void)
{
#if LCD_USE_DMA
  return LCD_DMA_IsBusy();
#else
  return 0U;
#endif
}

void LCD_CanvasWait(void)
{
#if LCD_USE_DMA
  LCD_DMA_Wait();
#endif
}

#endif /* LCD_USE_CANVAS */
