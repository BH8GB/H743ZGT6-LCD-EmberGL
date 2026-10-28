/* ILI9806G 800x480 液晶驱动（FMC 8080 并口） */
#include "lcd.h"

#if LCD_USE_TEXT
#include "lcd_font.h"
#endif

lcd_dev_t lcddev;

extern void LCD_PanelInitSequence(void);

#if LCD_USE_TEXT
static uint32_t lcd_pow10(uint8_t n);
#endif

/* ---- 背光 / 复位 ---- */
void LCD_Backlight(uint8_t on)
{
#if LCD_BL_ACTIVE_LEVEL
  if (on) LL_GPIO_SetOutputPin(LCD_BL_GPIO_Port, LCD_BL_Pin);
  else    LL_GPIO_ResetOutputPin(LCD_BL_GPIO_Port, LCD_BL_Pin);
#else
  if (on) LL_GPIO_ResetOutputPin(LCD_BL_GPIO_Port, LCD_BL_Pin);
  else    LL_GPIO_SetOutputPin(LCD_BL_GPIO_Port, LCD_BL_Pin);
#endif
}

static void LCD_HardwareReset(void)
{
#if LCD_USE_RST_PIN
  LL_GPIO_SetOutputPin(LCD_RST_GPIO_Port, LCD_RST_Pin);
  LCD_DELAY_MS(10);
  LL_GPIO_ResetOutputPin(LCD_RST_GPIO_Port, LCD_RST_Pin);
  LCD_DELAY_MS(20);
  LL_GPIO_SetOutputPin(LCD_RST_GPIO_Port, LCD_RST_Pin);
  LCD_DELAY_MS(120);
#else
  LCD_DELAY_MS(120);
#endif
}

/* 0xD3 读数为 0x00,0x98,0x06 -> id = 0x9806 */
uint16_t LCD_ReadID(void)
{
  uint16_t id;

#if LCD_CONTROLLER_NT35510
  /* 正点原子例程：先发厂家密钥，再读 C500/C501；NT35510 实际返回 0x8000，
     为便于识别统一记作 0x5510。 */
  LCD_WriteReg(0xF000, 0x55);
  LCD_WriteReg(0xF001, 0xAA);
  LCD_WriteReg(0xF002, 0x52);
  LCD_WriteReg(0xF003, 0x08);
  LCD_WriteReg(0xF004, 0x01);

  LCD_WriteCmd(0xC500);
  id = (uint16_t)(LCD_ReadData() << 8);
  LCD_WriteCmd(0xC501);
  id |= LCD_ReadData();

  LCD_DELAY_MS(5);
  if (id == 0x8000U) id = 0x5510U;
#else
  LCD_WriteCmd(0xD3);
  (void)LCD_ReadData();                       /* dummy */
  (void)LCD_ReadData();                       /* 0x00  */
  id  = (uint16_t)(LCD_ReadData() << 8);      /* 0x98  */
  id |= LCD_ReadData();                       /* 0x06  */
#endif
  return id;
}

void LCD_DisplayDir(uint8_t dir)
{
  uint8_t madctl;

  switch (dir)
  {
    case 0:
      lcddev.width = LCD_PANEL_PORTRAIT_W; lcddev.height = LCD_PANEL_PORTRAIT_H;
      madctl = LCD_MADCTL_DIR0;
      break;
    case 2:
      lcddev.width = LCD_PANEL_PORTRAIT_W; lcddev.height = LCD_PANEL_PORTRAIT_H;
      madctl = LCD_MADCTL_DIR2;
      break;
    case 3:
      lcddev.width = LCD_PANEL_PORTRAIT_H; lcddev.height = LCD_PANEL_PORTRAIT_W;
      madctl = LCD_MADCTL_DIR3;
      break;
    case 1:
    default:
      lcddev.width = LCD_PANEL_PORTRAIT_H; lcddev.height = LCD_PANEL_PORTRAIT_W;
      madctl = LCD_MADCTL_DIR1;
      dir = 1;
      break;
  }

  lcddev.dir = dir;
  lcddev.bgr = 1;

#if LCD_CONTROLLER_NT35510
  /* NT35510 不需要 BGR 位（正点原子例程只对 ILI9341/ST7789 置 0x08）。
     置了会红蓝互换、黄变青。 */
#else
#if (LCD_ILI9806_BGR != 0)
  madctl |= 0x08U;
#endif
#endif
#if LCD_CONTROLLER_NT35510
  LCD_WriteReg(0x3600, madctl);
#else
  LCD_WriteReg(0x36, madctl);
#endif
}

void LCD_Init(void)
{
  LCD_Backlight(0);
  LCD_Bus_Init();
  LCD_HardwareReset();

  lcddev.id = LCD_ReadID();

  LCD_PanelInitSequence();
  LCD_DisplayDir(LCD_DEFAULT_DIR);

  /* NT35510 的 sleep/display 命令是 16 位（0x1100 / 0x2900），初始化表里已含 0x3500/0x3A00 */
#if LCD_CONTROLLER_NT35510
  LCD_WriteCmd(0x1100);                       /* Exit Sleep */
#else
  LCD_WriteCmd(0x11);                         /* Exit Sleep */
#endif
  LCD_DELAY_MS(120);
#if LCD_CONTROLLER_NT35510
  LCD_WriteCmd(0x2900);                       /* Display On */
#else
  LCD_WriteCmd(0x29);                         /* Display On */
#endif
  LCD_DELAY_MS(50);

  LCD_Clear(BLACK);
  LCD_Backlight(1);
}

#if LCD_CONTROLLER_NT35510
void LCD_DisplayOn(void)  { LCD_WriteCmd(0x2900); }
void LCD_DisplayOff(void) { LCD_WriteCmd(0x2800); }
#else
void LCD_DisplayOn(void)  { LCD_WriteCmd(0x29); }
void LCD_DisplayOff(void) { LCD_WriteCmd(0x28); }
#endif

/* ---- 开窗 / 光标 ---- */
void LCD_SetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
  if ((xs >= lcddev.width) || (ys >= lcddev.height)) return;
  if (xe >= lcddev.width)  xe = (uint16_t)(lcddev.width  - 1U);
  if (ye >= lcddev.height) ye = (uint16_t)(lcddev.height - 1U);
  if (xs > xe) xs = xe;
  if (ys > ye) ys = ye;

#if LCD_CONTROLLER_NT35510
  /* NT35510：0x2A00~0x2A03 / 0x2B00~0x2B03，每条命令带 1 个参数字节 */
  LCD_WriteCmd(0x2A00); LCD_WriteData((uint16_t)(xs >> 8));
  LCD_WriteCmd(0x2A01); LCD_WriteData((uint16_t)(xs & 0xFF));
  LCD_WriteCmd(0x2A02); LCD_WriteData((uint16_t)(xe >> 8));
  LCD_WriteCmd(0x2A03); LCD_WriteData((uint16_t)(xe & 0xFF));

  LCD_WriteCmd(0x2B00); LCD_WriteData((uint16_t)(ys >> 8));
  LCD_WriteCmd(0x2B01); LCD_WriteData((uint16_t)(ys & 0xFF));
  LCD_WriteCmd(0x2B02); LCD_WriteData((uint16_t)(ye >> 8));
  LCD_WriteCmd(0x2B03); LCD_WriteData((uint16_t)(ye & 0xFF));

  LCD_WriteCmd(0x2C00);
#else
  LCD_WriteCmd(0x2A);
  LCD_WriteData((uint16_t)(xs >> 8));
  LCD_WriteData((uint16_t)(xs & 0xFF));
  LCD_WriteData((uint16_t)(xe >> 8));
  LCD_WriteData((uint16_t)(xe & 0xFF));

  LCD_WriteCmd(0x2B);
  LCD_WriteData((uint16_t)(ys >> 8));
  LCD_WriteData((uint16_t)(ys & 0xFF));
  LCD_WriteData((uint16_t)(ye >> 8));
  LCD_WriteData((uint16_t)(ye & 0xFF));

  LCD_WriteCmd(0x2C);
#endif
}

void LCD_SetCursor(uint16_t x, uint16_t y) { LCD_SetWindow(x, y, x, y); }

/* ---- 画点 ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
  LCD_SetWindow(x, y, x, y);
  LCD_WriteData(color);
}

uint16_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
  uint16_t color;

  LCD_SetWindow(x, y, x, y);
#if LCD_CONTROLLER_NT35510
  LCD_WriteCmd(0x2E00);
#else
  LCD_WriteCmd(0x2E);
#endif
  (void)LCD_ReadData();
  color = LCD_ReadData();
#if LCD_CONTROLLER_NT35510
  LCD_WriteCmd(0x2C00);                       /* 读会打断写方向，恢复 */
#else
  LCD_WriteCmd(0x2C);                         /* 读会打断写方向，恢复 */
#endif
  return color;
}

/* ---- 填充 ---- */
void LCD_Fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color)
{
  uint32_t n;

  if (xs > xe) { uint16_t t = xs; xs = xe; xe = t; }
  if (ys > ye) { uint16_t t = ys; ys = ye; ye = t; }
  if (xs >= lcddev.width || ys >= lcddev.height) return;
  if (xe >= lcddev.width)  xe = (uint16_t)(lcddev.width  - 1U);
  if (ye >= lcddev.height) ye = (uint16_t)(lcddev.height - 1U);

  n = (uint32_t)(xe - xs + 1) * (uint32_t)(ye - ys + 1);
  LCD_SetWindow(xs, ys, xe, ye);
  LCD_WriteDataRepeat(n, color);
}

void LCD_Clear(uint16_t color)
{
  LCD_Fill(0, 0, (uint16_t)(lcddev.width - 1), (uint16_t)(lcddev.height - 1), color);
}

void LCD_ColorFill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const uint16_t *buf)
{
  if (xs > xe) { uint16_t t = xs; xs = xe; xe = t; }
  if (ys > ye) { uint16_t t = ys; ys = ye; ye = t; }

  LCD_BlitArea(xs, ys, (uint16_t)(xe - xs + 1U), (uint16_t)(ye - ys + 1U),
               buf, (uint16_t)(xe - xs + 1U));
}

/* ---- 图形库适配原语 ---- */
void LCD_WritePixels(const uint16_t *buf, uint32_t n) { LCD_WriteDataBlock(buf, n); }
void LCD_WritePixelRepeat(uint16_t color, uint32_t n) { LCD_WriteDataRepeat(n, color); }

void LCD_DrawSpan(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
  if (len == 0U) return;
  LCD_Fill(x, y, (uint16_t)(x + len - 1U), y, color);
}

void LCD_BlitSpan(uint16_t x, uint16_t y, uint16_t len, const uint16_t *src)
{
  if (len == 0U) return;
  LCD_SetWindow(x, y, (uint16_t)(x + len - 1U), y);
  LCD_WriteDataBlock(src, len);
}

/* pitch == w 时整块连续，只开一次窗（最快）；否则逐行开窗 */
void LCD_BlitArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const uint16_t *src, uint16_t pitch)
{
  const uint16_t *s = src;
  uint16_t row;

  if ((w == 0U) || (h == 0U) || (src == 0)) return;
  if (pitch == 0U) pitch = w;
  if ((x >= lcddev.width) || (y >= lcddev.height)) return;

  if (((uint32_t)x + w) > lcddev.width)  w = (uint16_t)(lcddev.width  - x);
  if (((uint32_t)y + h) > lcddev.height) h = (uint16_t)(lcddev.height - y);
  if ((w == 0U) || (h == 0U)) return;

  if (w == pitch)
  {
    LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    LCD_WriteDataBlock(s, (uint32_t)w * (uint32_t)h);
    return;
  }

  for (row = 0U; row < h; row++)
  {
    LCD_SetWindow(x, (uint16_t)(y + row), (uint16_t)(x + w - 1U), (uint16_t)(y + row));
    LCD_WriteDataBlock(s, w);
    s += pitch;
  }
}

uint16_t LCD_GetWidth(void)  { return lcddev.width; }
uint16_t LCD_GetHeight(void) { return lcddev.height; }

/* ---- 线 / 矩形 / 圆 ----
 * 8080 屏每画一个点都要先写 0x2A/0x2B/0x2C 共 11 个总线周期（约 600ns），
 * 远贵于数据本身。所以水平/垂直线走 LCD_Fill（一次开窗 + 连续写），
 * 斜线把同排/同列的连续像素合并成段。 */
void LCD_DrawHLine(uint16_t x1, uint16_t x2, uint16_t y, uint16_t color)
{
  if (x1 > x2) { uint16_t t = x1; x1 = x2; x2 = t; }
  LCD_Fill(x1, y, x2, y, color);
}

void LCD_DrawVLine(uint16_t x, uint16_t y1, uint16_t y2, uint16_t color)
{
  if (y1 > y2) { uint16_t t = y1; y1 = y2; y2 = t; }
  LCD_Fill(x, y1, x, y2, color);
}

typedef struct { uint16_t color; } lcd_line_ctx_t;

static void lcd_line_run_cb(void *ctx, int32_t x, int32_t y, int32_t len, int vertical)
{
  const lcd_line_ctx_t *lc = (const lcd_line_ctx_t *)ctx;
  int32_t w = (int32_t)lcddev.width;
  int32_t h = (int32_t)lcddev.height;

  if (vertical != 0)
  {
    int32_t y0 = y, y1 = y + len - 1;

    if (x < 0 || x >= w || len <= 0) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= h) y1 = h - 1;
    if (y0 > y1) return;
    LCD_Fill((uint16_t)x, (uint16_t)y0, (uint16_t)x, (uint16_t)y1, lc->color);
  }
  else
  {
    int32_t x0 = x, x1 = x + len - 1;

    if (y < 0 || y >= h || len <= 0) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (x0 > x1) return;
    LCD_Fill((uint16_t)x0, (uint16_t)y, (uint16_t)x1, (uint16_t)y, lc->color);
  }
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
  lcd_line_ctx_t lc;

  lc.color = color;
  LCD_RasterLine((int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2, lcd_line_run_cb, &lc);
}

void LCD_DrawRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
  if (x1 > x2) { uint16_t t = x1; x1 = x2; x2 = t; }
  if (y1 > y2) { uint16_t t = y1; y1 = y2; y2 = t; }

  LCD_DrawHLine(x1, x2, y1, color);
  LCD_DrawHLine(x1, x2, y2, color);
  LCD_DrawVLine(x1, y1, y2, color);
  LCD_DrawVLine(x2, y1, y2, color);
}

void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
  int32_t a = 0, b = r;
  int32_t d = 3 - 2 * (int32_t)r;

  while (a <= b)
  {
    LCD_DrawPoint((uint16_t)(x0 + a), (uint16_t)(y0 + b), color);
    LCD_DrawPoint((uint16_t)(x0 - a), (uint16_t)(y0 + b), color);
    LCD_DrawPoint((uint16_t)(x0 + a), (uint16_t)(y0 - b), color);
    LCD_DrawPoint((uint16_t)(x0 - a), (uint16_t)(y0 - b), color);
    LCD_DrawPoint((uint16_t)(x0 + b), (uint16_t)(y0 + a), color);
    LCD_DrawPoint((uint16_t)(x0 - b), (uint16_t)(y0 + a), color);
    LCD_DrawPoint((uint16_t)(x0 + b), (uint16_t)(y0 - a), color);
    LCD_DrawPoint((uint16_t)(x0 - b), (uint16_t)(y0 - a), color);

    if (d < 0) d += 4 * a + 6;
    else     { d += 10 + 4 * (a - b); b--; }
    a++;
  }
}

/* ---- 字符（字库按列存放，每列 size/8 字节，字节内高位在上）---- */
#if LCD_USE_TEXT
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size,
                  uint16_t fc, uint16_t bc)
{
  const unsigned char *pdat;
  uint8_t idx = (uint8_t)(ch - ' ');
  uint8_t bytes_per_col, cols, c, r, b, bit;

  if (size != 16 && size != 24 && size != 32) return;
  if (idx > 94) return;
  if ((x + size / 2) > lcddev.width || (y + size) > lcddev.height) return;

  switch (size)
  {
    case 16: pdat = (const unsigned char *)asc2_1608[idx]; break;
    case 24: pdat = (const unsigned char *)asc2_2412[idx]; break;
    default: pdat = (const unsigned char *)asc2_3216[idx]; break;
  }

  bytes_per_col = (uint8_t)(size / 8);
  cols          = (uint8_t)(size / 2);

  /* 字模是列行式（一整列 size 位，再下一列），GRAM 按行接收。
     开一次窗、按行重排后连续写入，避免逐点开窗（每点 18 条总线命令）。 */
  LCD_SetWindow(x, y, (uint16_t)(x + cols - 1U), (uint16_t)(y + size - 1U));

  for (r = 0; r < size; r++)
  {
    b   = (uint8_t)(r >> 3);
    bit = (uint8_t)(7U - (r & 7U));

    for (c = 0; c < cols; c++)
    {
      LCD_WriteData((pdat[(uint16_t)c * bytes_per_col + b] & (1U << bit)) ? fc : bc);
    }
  }
}

void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t size,
                    uint16_t fc, uint16_t bc)
{
  uint16_t xs = x;

  while (*str != '\0')
  {
    if (*str == '\r') { str++; continue; }
    if (*str == '\n')
    {
      xs = x;
      y  = (uint16_t)(y + size);
      str++;
      continue;
    }
    LCD_ShowChar(xs, y, *str, size, fc, bc);
    xs = (uint16_t)(xs + size / 2);
    str++;
  }
}

void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size,
                 uint16_t fc, uint16_t bc)
{
  uint8_t t, i;

  for (i = 0; i < len; i++)
  {
    t = (uint8_t)((num / lcd_pow10(len - 1 - i)) % 10 + '0');
    LCD_ShowChar((uint16_t)(x + (size / 2) * i), y, (char)t, size, fc, bc);
  }
}

static uint32_t lcd_pow10(uint8_t n)
{
  uint32_t r = 1;
  while (n--) r *= 10U;
  return r;
}
#endif /* LCD_USE_TEXT */
