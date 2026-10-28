/* EmberGL 设备层实现：把光栅化器的 tile 脏矩形直接推到屏。 */
#include "lcd_conf.h"

#if LCD_PORT_EMBERGL

#include "lcd_port_embergl.h"
#include <new>                      /* placement new */

extern "C" {
#include "lcd.h"
#include "lcd_canvas.h"
}

EGL_USING_NAMESPACE

/* 光栅化器内存交给链接器分配（放在 .bss，DTCM）。
   手动放在 AXI SRAM 固定地址容易与画布/其他缓冲撞车，
   而且 sizeof 变化时不会报错。这里约 39KB，DTCM 128KB 够用。 */
static LCD_BUF_ATTR unsigned char s_egl_mem_buf[sizeof(lcd_embergl_mem_t)];
static lcd_embergl_mem_t *s_egl_mem = 0;
static graphics_device_lcd s_egl_dev;
static uint8_t s_egl_ready = 0U;

static_assert((LCD_EGL_TILE_W & (LCD_EGL_TILE_W - 1U)) == 0U, "tile width must be pow2");
static_assert((LCD_EGL_TILE_H & (LCD_EGL_TILE_H - 1U)) == 0U, "tile height must be pow2");
static_assert(LCD_EGL_TILE_W <= LCD_EGL_WIDTH,  "tile wider than screen");
static_assert(LCD_EGL_TILE_H <= LCD_EGL_HEIGHT, "tile taller than screen");
static_assert((int)lcd_embergl_mem_cfg::rt0_fmt == (int)graphics_device_lcd::fb_format,
              "rt0_fmt must match fb_format");

graphics_device_lcd::graphics_device_lcd()
  : m_tile_rt0(0), m_tile_width(0), m_tile_height(0), m_pushed_px(0U)
{
}

void graphics_device_lcd::init_rasterizer(const rasterizer_cfg &rcfg_,
                                          const rasterizer_tiling_cfg &tcfg_,
                                          const rasterizer_vertex_cache_cfg &vccfg_)
{
  graphics_device<graphics_device_lcd>::init(rcfg_, tcfg_, vccfg_);

  m_tile_rt0    = static_cast<fb_format_t *>(rcfg_.rts[0]);
  m_tile_width  = tcfg_.tile_width;
  m_tile_height = tcfg_.tile_height;
}

void graphics_device_lcd::init_dma(rasterizer_data_transfer*, uint8_t, void*, size_t)
{
}

/* 半屏离屏缓冲：800x240x2 = 375KB。
   AXI SRAM 实际范围 0x24000000~0x2407FFFF（512KB）。
   画布占 0x24000000 起 50KB，缓冲从 0x2400C800 起，全部落在范围内。
   渲染时先写离屏缓冲，整帧完成后再一次推屏，避免看到 tile 逐块绘制。 */
#define LCD_EGL_FB_BASE    (0x2400C800UL)
#define LCD_EGL_FB_LINES   (240U)
#define LCD_EGL_FB_PIXELS  (LCD_EGL_WIDTH * 64U)   /* 64 行一条，分 4 条推 */

static uint16_t *const s_egl_fb = (uint16_t *)LCD_EGL_FB_BASE;
static uint16_t s_egl_fb_y0 = 0U;
static uint16_t s_egl_fb_y1 = 0U;

/* 核心：把 tile 内的脏矩形 [reg_min, reg_end) 写进离屏缓冲 */
void graphics_device_lcd::submit_tile(uint8_t tx_, uint8_t ty_,
                                      const vec2u16 &reg_min_, const vec2u16 &reg_end_,
                                      uint16_t thread_idx_)
{
  (void)thread_idx_;

  uint16_t w = (uint16_t)(reg_end_.x - reg_min_.x);
  uint16_t h = (uint16_t)(reg_end_.y - reg_min_.y);
  if ((w == 0U) || (h == 0U) || (m_tile_rt0 == 0)) return;

  uint16_t x = (uint16_t)(tx_ * m_tile_width  + reg_min_.x);
  uint16_t y = (uint16_t)(ty_ * m_tile_height + reg_min_.y);
  const uint16_t *src = reinterpret_cast<const uint16_t *>(
      m_tile_rt0 + reg_min_.x + reg_min_.y * m_tile_width);

  /* 裁剪到当前条带 [s_egl_fb_y0, s_egl_fb_y1)，条带外的行直接丢弃 */
  {
    uint16_t row0 = 0U;
    uint16_t rowN = h;
    int32_t  y_end = (int32_t)y + (int32_t)h;

    /* 完全在条带下方：丢弃 */
    if ((int32_t)y >= (int32_t)s_egl_fb_y1)
    {
      m_pushed_px += (uint32_t)w * (uint32_t)h;
      return;
    }
    /* 完全在条带上方：丢弃 */
    if (y_end <= (int32_t)s_egl_fb_y0)
    {
      m_pushed_px += (uint32_t)w * (uint32_t)h;
      return;
    }

    if ((int32_t)y < (int32_t)s_egl_fb_y0) { row0 = (uint16_t)((int32_t)s_egl_fb_y0 - (int32_t)y); }
    if (y_end > (int32_t)s_egl_fb_y1)       { rowN = (uint16_t)((int32_t)s_egl_fb_y1 - (int32_t)y); }

    if (row0 < rowN)
    {
      uint16_t col;
      uint16_t row;
      const uint16_t *s = src + (uint32_t)row0 * m_tile_width;
      uint16_t *dst = s_egl_fb + (uint32_t)(y + row0 - s_egl_fb_y0) * LCD_EGL_WIDTH + x;

      for (row = row0; row < rowN; ++row)
      {
        for (col = 0U; col < w; ++col) { dst[col] = s[col]; }
        dst += LCD_EGL_WIDTH;
        s   += m_tile_width;
      }
    }
  }
  m_pushed_px += (uint32_t)w * (uint32_t)h;
}

extern "C" int LCD_EGL_Init(void)
{
  if ((lcddev.width != LCD_EGL_WIDTH) || (lcddev.height != LCD_EGL_HEIGHT))
  {
    return 1;                     /* 分辨率不匹配：查 LCD_DEFAULT_DIR / LCD_EGL_WIDTH */
  }

  if (s_egl_mem == 0)
  {
    s_egl_mem = new (s_egl_mem_buf) lcd_embergl_mem_t();
  }
  s_egl_mem->init(s_egl_dev);
  s_egl_dev.reset_pushed_pixels();
  s_egl_ready = 1U;
  return 0;
}

extern "C" void LCD_EGL_BeginFrame(uint16_t rgb565_clear_color)
{
  if (!s_egl_ready) return;

  LCD_Clear(rgb565_clear_color);
  s_egl_dev.clear_depth(cleardepth_max);   /* 深度测试用 LESS，清成最大值 */
  s_egl_dev.reset_pushed_pixels();
}

extern "C" void LCD_EGL_BeginFramePartial(uint16_t rgb565_clear_color,
                                          uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h)
{
  if (!s_egl_ready) return;
  if ((w == 0U) || (h == 0U)) return;

  /* 只清上一帧的脏矩形：整屏清屏要写 750KB，是撕裂和掉帧的主因。 */
  LCD_Fill(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U), rgb565_clear_color);
  s_egl_dev.clear_depth(cleardepth_max);
  s_egl_dev.reset_pushed_pixels();
}

/* 开始一帧离屏渲染：把缓冲对应的屏幕行范围设为 [y0, y1) */
extern "C" void LCD_EGL_BeginFrameBuffered(uint16_t rgb565_clear_color,
                                           uint16_t y0, uint16_t band_h)
{
  uint32_t i;
  uint32_t n;

  if (!s_egl_ready) return;
  if (band_h == 0U) band_h = LCD_EGL_FB_LINES;
  if ((uint32_t)y0 + band_h > LCD_EGL_HEIGHT) band_h = (uint16_t)(LCD_EGL_HEIGHT - y0);

  s_egl_fb_y0 = y0;
  s_egl_fb_y1 = (uint16_t)(y0 + band_h);

  n = (uint32_t)LCD_EGL_WIDTH * band_h;
  for (i = 0U; i < n; ++i) { s_egl_fb[i] = rgb565_clear_color; }

  s_egl_dev.clear_depth(cleardepth_max);
  s_egl_dev.reset_pushed_pixels();
}

/* 把离屏缓冲整条推到屏：屏幕不会看到逐 tile 绘制的中间态 */
extern "C" void LCD_EGL_PresentBand(void)
{
  uint16_t h;

  if (!s_egl_ready) return;
  h = (uint16_t)(s_egl_fb_y1 - s_egl_fb_y0);
  if (h == 0U) return;

  LCD_BlitArea(0U, s_egl_fb_y0, LCD_EGL_WIDTH, h, s_egl_fb, LCD_EGL_WIDTH);
}

extern "C" void LCD_EGL_EndFrame(void)
{
  /* submit_tile 里已经同步写完总线，这里没有待办 */
}

extern "C" uint32_t LCD_EGL_PushedPixels(void) { return s_egl_dev.pushed_pixels(); }

graphics_device_lcd &LCD_EGL_Device(void) { return s_egl_dev; }
lcd_embergl_mem_t   &LCD_EGL_Memory(void) { return *s_egl_mem; }

#endif /* LCD_PORT_EMBERGL */
