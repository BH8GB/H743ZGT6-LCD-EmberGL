/* 屏幕自检：先测整屏填充速度，再画色条/渐变/参数；触摸测试单独调用 */
#include "lcd_selftest.h"
#include "lcd.h"
#include "gt911.h"
#include "stm32h7xx_hal.h"

#ifndef LCD_TOUCH_ROT90
  #define LCD_TOUCH_ROT90         (1)   /* 面板竖装 480x800，显示横屏 800x480 */
#endif

#define TEST_ROW_MAX   (LCD_PANEL_PORTRAIT_H)

static uint16_t s_row[TEST_ROW_MAX];

/* NT35510 的 RGB565 与 ILI 系列位序相反：R 在低 5 位、B 在高 5 位。
   面板实际按 BGR 收数据，所以渐变要按这个顺序拼。 */
static uint16_t rgb565_panel(uint8_t r, uint8_t g, uint8_t b)
{
#if LCD_CONTROLLER_NT35510
  return (uint16_t)(((uint16_t)(b & 0x1FU) << 11) |
                    ((uint16_t)(g & 0x3FU) << 5)  |
                    (uint16_t)(r & 0x1FU));
#else
  return (uint16_t)(((uint16_t)(r & 0x1FU) << 11) |
                    ((uint16_t)(g & 0x3FU) << 5)  |
                    (uint16_t)(b & 0x1FU));
#endif
}

static void draw_color_bars(uint16_t y0, uint16_t hh)
{
  static const uint16_t colors[8] = {
    WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, BLACK
  };
  uint16_t w = LCD_GetWidth();
  uint16_t i;

  for (i = 0U; i < 8U; i++)
  {
    uint16_t x0 = (uint16_t)(((uint32_t)i * w) / 8U);
    uint16_t x1 = (uint16_t)((((uint32_t)(i + 1U) * w) / 8U) - 1U);

    LCD_Fill(x0, y0, x1, (uint16_t)(y0 + hh - 1U), colors[i]);
  }
}

static void draw_gradient(uint16_t y0, uint16_t hh)
{
  uint16_t w = LCD_GetWidth();
  uint16_t x, y;
  uint16_t wd = (uint16_t)((w > 1U) ? (w - 1U) : 1U);
  uint16_t hd = (uint16_t)((hh > 1U) ? (hh - 1U) : 1U);

  for (y = 0U; y < hh; y++)
  {
    uint8_t g = (uint8_t)(((uint32_t)y * 63U) / hd);

    for (x = 0U; x < w; x++)
    {
      uint8_t r = (uint8_t)(((uint32_t)x * 31U) / wd);
      uint8_t b = (uint8_t)(31U - r);

      s_row[x] = rgb565_panel(r, g, b);
    }

    LCD_ColorFill(0U, (uint16_t)(y0 + y), (uint16_t)(w - 1U),
                  (uint16_t)(y0 + y), s_row);
  }
}

void LCD_SelfTest(void)
{
  uint16_t w = LCD_GetWidth();
  uint16_t h = LCD_GetHeight();
  uint32_t t0, dt, ms, fps;
  uint32_t i;
  const uint32_t frames = 10U;

  /* 整屏填充：每帧 800x480x2B = 768 KB，反映 FMC 实际带宽 */
  t0 = HAL_GetTick();
  for (i = 0U; i < frames; i++)
  {
    LCD_Clear(((i & 1U) != 0U) ? BLACK : WHITE);
  }
  dt = HAL_GetTick() - t0;
  ms = (uint32_t)((dt + (frames / 2U)) / frames);
  fps = (dt > 0U) ? ((uint32_t)((frames * 1000U + (dt / 2U)) / dt)) : 0U;

  LCD_Clear(BLACK);
  LCD_DrawRect(4, 4, (uint16_t)(w - 5U), (uint16_t)(h - 5U), GRAY);
  LCD_ShowString(8, 6, "NT35510 SELF TEST", 24, WHITE, BLACK);

  LCD_ShowString(8, 38, "ID", 24, CYAN, BLACK);
  LCD_ShowNum(44, 38, lcddev.id, 4, 24, CYAN, BLACK);
  LCD_ShowString(120, 38, "RES", 24, CYAN, BLACK);
  LCD_ShowNum(180, 38, w, 3, 24, CYAN, BLACK);
  LCD_ShowString(228, 38, "x", 24, CYAN, BLACK);
  LCD_ShowNum(244, 38, h, 3, 24, CYAN, BLACK);

  draw_color_bars(72U, 120U);
  draw_gradient(200U, 120U);

  LCD_ShowString(8, 330, "FULLSCREEN", 24, LGRAY, BLACK);
  LCD_ShowNum(200, 330, ms, 3, 24, YELLOW, BLACK);
  LCD_ShowString(260, 330, "ms", 24, LGRAY, BLACK);
  LCD_ShowNum(320, 330, fps, 3, 24, GREEN, BLACK);
  LCD_ShowString(380, 330, "fps", 24, LGRAY, BLACK);

  LCD_ShowString(8, 366, "FMC WR DATAST", 24, LGRAY, BLACK);
  LCD_ShowNum(260, 366, LCD_Bus_GetWriteTiming(), 2, 24, LGRAY, BLACK);

  LCD_ShowString(8, 402, "TOUCH TO TEST", 24, MAGENTA, BLACK);
  LCD_Backlight(1);
}

/* 触摸测试的跨帧状态（轨迹连线的上一个点、累计点数、分辨率） */
static uint16_t ts_w, ts_h, ts_resx, ts_resy;
static uint32_t ts_pts;
static uint16_t ts_last_x, ts_last_y;
static uint8_t  ts_has_last;

/* 只做画面与状态初始化；任务里随后反复调用 LCD_TouchLoop。 */
void LCD_TouchInit(void)
{
  uint16_t w, h;

  w = ts_w = LCD_GetWidth();
  h = ts_h = LCD_GetHeight();
  ts_pts = 0U;
  ts_has_last = 0U;

  GT911_GetResolution(&ts_resx, &ts_resy);
  /* 分辨率寄存器读失败时会是 0 或 0xFFFF，此时退回面板尺寸 */
  if ((ts_resx < 100U) || (ts_resx > 2000U)) ts_resx = w;
  if ((ts_resy < 100U) || (ts_resy > 2000U)) ts_resy = h;
  if ((ts_resx == 0U) || (ts_resy == 0U))
  {
    ts_resx = w;
    ts_resy = h;
  }

  LCD_Clear(BLACK);
  LCD_ShowString(8, 6, "TOUCH TEST", 24, WHITE, BLACK);
  LCD_ShowString(8, 38, "N", 24, LGRAY, BLACK);
  LCD_ShowNum(32, 38, 0U, 6, 24, GREEN, BLACK);
  LCD_ShowString(220, 38, "X", 24, LGRAY, BLACK);
  LCD_ShowNum(244, 38, 0U, 4, 24, CYAN, BLACK);
  LCD_ShowString(304, 38, "Y", 24, LGRAY, BLACK);
  LCD_ShowNum(328, 38, 0U, 4, 24, CYAN, BLACK);
  LCD_ShowString(420, 38, "RES", 24, LGRAY, BLACK);
  LCD_ShowNum(470, 38, ts_resx, 4, 24, YELLOW, BLACK);
  LCD_ShowNum(540, 38, ts_resy, 4, 24, YELLOW, BLACK);
  LCD_ShowString(420, 6, "RAW", 24, LGRAY, BLACK);
  LCD_ShowString(420, 70, "DX", 24, LGRAY, BLACK);
  LCD_ShowNum(470, 70, 0U, 4, 24, GREEN, BLACK);
  LCD_ShowString(540, 70, "DY", 24, LGRAY, BLACK);
  LCD_ShowNum(590, 70, 0U, 4, 24, GREEN, BLACK);
  LCD_DrawRect(4, 70, (uint16_t)(w - 5U), (uint16_t)(h - 5U), GRAY);
}

/* FreeRTOS 任务里调用：扫描一次、画一笔、让出 delay_ms。 */
void LCD_TouchLoop(uint32_t delay_ms)
{
  const ct_point_t *p;
  uint16_t rx, ry, dx, dy;

  if (GT911_Scan() == 0U)
  {
    ts_has_last = 0U;      /* 抬手后断开轨迹 */
    LCD_DELAY_MS(delay_ms);
    return;
  }

  p  = GT911_GetPoints();
  rx = p->x;
  ry = p->y;

#if LCD_TOUCH_ROT90
  /* 面板竖装 480x800，显示旋转成横屏 800x480：
     X 屏 = 触摸 Y 的镜像，Y 屏 = 触摸 X。 */
  dx = (uint16_t)(((uint32_t)((ry < ts_resy) ? (ts_resy - 1U - ry) : 0U) * (ts_w - 1U)) / ts_resy);
  dy = (uint16_t)(((uint32_t)((rx < ts_resx) ? rx : (ts_resx - 1U)) * (ts_h - 1U)) / ts_resx);
#else
  dx = (uint16_t)(((uint32_t)rx * (ts_w - 1U)) / ts_resx);
  dy = (uint16_t)(((uint32_t)ry * (ts_h - 1U)) / ts_resy);
#endif

  if ((dx < ts_w) && (dy < ts_h))
  {
    if (ts_has_last != 0U)
    {
      LCD_DrawLine(ts_last_x, ts_last_y, dx, dy, WHITE);
    }
    else
    {
      ts_has_last = 1U;
    }

    LCD_Fill((uint16_t)((dx > 2U) ? (dx - 2U) : 0U),
             (uint16_t)((dy > 2U) ? (dy - 2U) : 0U),
             (uint16_t)((dx + 2U < ts_w) ? (dx + 2U) : (ts_w - 1U)),
             (uint16_t)((dy + 2U < ts_h) ? (dy + 2U) : (ts_h - 1U)),
             WHITE);

    ts_last_x = dx;
    ts_last_y = dy;
  }

  ts_pts++;
  LCD_ShowNum(32, 38, ts_pts, 6, 24, GREEN, BLACK);
  LCD_ShowNum(244, 38, rx, 4, 24, CYAN, BLACK);
  LCD_ShowNum(328, 38, ry, 4, 24, CYAN, BLACK);
  LCD_ShowNum(470, 6, rx, 4, 24, RED, BLACK);
  LCD_ShowNum(540, 6, ry, 4, 24, RED, BLACK);
  LCD_ShowNum(470, 70, dx, 4, 24, GREEN, BLACK);
  LCD_ShowNum(590, 70, dy, 4, 24, GREEN, BLACK);

  LCD_DELAY_MS(delay_ms);
}

/* 单比特色条：第 i 根 = (1<<i)。
 * 某根全黑 -> 该数据线断；颜色顺序错 -> BGR/MADCTL；位置错 -> 地址线接错。 */
void LCD_BitTest(void)
{
  uint16_t w = LCD_GetWidth();
  uint16_t h = LCD_GetHeight();
  uint16_t i;
  uint16_t bar = (uint16_t)(w / 16U);

  LCD_Clear(BLACK);

  for (i = 0U; i < 16U; i++)
  {
    uint16_t x0 = (uint16_t)(i * bar);
    uint16_t x1 = (uint16_t)((i == 15U) ? (w - 1U) : ((i + 1U) * bar - 1U));

    LCD_Fill(x0, 0U, x1, (uint16_t)(h - 1U), (uint16_t)(1U << i));
  }

  LCD_Backlight(1);
}

/* 几何测试：用已知坐标画线，验证开窗与坐标映射。
 * 白框 = 屏幕边界，红=顶边 y=0，绿=底边 y=h-1，蓝=左边 x=0，黄=右边 x=w-1，
 * 紫十字 = 屏幕中心。任一条不贴边或位置错，就是坐标/开窗问题。 */
void LCD_GeoTest(void)
{
  uint16_t w = LCD_GetWidth();
  uint16_t h = LCD_GetHeight();
  uint16_t cx = (uint16_t)(w / 2U);
  uint16_t cy = (uint16_t)(h / 2U);

  LCD_Clear(BLACK);

  LCD_DrawHLine(0U, (uint16_t)(w - 1U), 0U, RED);
  LCD_DrawHLine(0U, (uint16_t)(w - 1U), (uint16_t)(h - 1U), GREEN);
  LCD_DrawVLine(0U, 0U, (uint16_t)(h - 1U), BLUE);
  LCD_DrawVLine((uint16_t)(w - 1U), 0U, (uint16_t)(h - 1U), YELLOW);

  LCD_DrawHLine((uint16_t)(cx - 60U), (uint16_t)(cx + 60U), cy, MAGENTA);
  LCD_DrawVLine(cx, (uint16_t)(cy - 60U), (uint16_t)(cy + 60U), MAGENTA);

  /* 左上角 40x40 纯红方块，位置固定的参照物 */
  LCD_Fill(2U, 2U, 42U, 42U, RED);
  /* 右上角 40x40 纯绿方块 */
  LCD_Fill((uint16_t)(w - 43U), 2U, (uint16_t)(w - 3U), 42U, GREEN);

  LCD_Backlight(1);
}

/* 字体测试：用逐点绘制和整窗绘制各写一遍同一行，
 * 对比两块是否一致，从而判断问题出在字模还是开窗。 */
void LCD_FontTest(void)
{
  uint16_t y0 = 40U, y1 = 200U;

  LCD_Clear(BLACK);

  LCD_ShowString(20U, y0, "0123456789", 24U, WHITE, BLACK);
  LCD_ShowString(20U, y1, "ABCDEFGHIJ", 24U, WHITE, BLACK);

  LCD_Backlight(1);
}
