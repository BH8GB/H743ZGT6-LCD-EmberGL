/* EmberGL 适配层：光栅化器按 tile 渲染，每完成一个 tile 就把 tile 内的脏矩形
 * 回调出来；这里把它直接接到 LCD（开窗 + 连续写），所以不需要整屏 framebuffer，
 * 只要 tile 缓冲（32x32 → 颜色 2KB + 深度 2KB）。
 *
 * 上游: https://github.com/EmberGL-org/EmberGL
 *       src/egl_device.h 的 graphics_device_mem / submit_tile(tx,ty,reg_min,reg_end,thread_idx)
 * 启用: lcd_conf.h 里 LCD_PORT_EMBERGL=1（需要 C++ 编译器）
 * 用法: LCD_EGL_Init(); LCD_EGL_BeginFrame(color); ...绘制...; LCD_EGL_EndFrame(); */
#ifndef __LCD_PORT_EMBERGL_H
#define __LCD_PORT_EMBERGL_H

#include "lcd_conf.h"

#ifndef LCD_PORT_EMBERGL
  #define LCD_PORT_EMBERGL      (0)
#endif

#if LCD_PORT_EMBERGL

#ifndef LCD_EGL_WIDTH
  #define LCD_EGL_WIDTH         LCD_PANEL_PORTRAIT_H     /* 800 */
#endif
#ifndef LCD_EGL_HEIGHT
  #define LCD_EGL_HEIGHT        LCD_PANEL_PORTRAIT_W     /* 480 */
#endif
#ifndef LCD_EGL_TILE_W
  #define LCD_EGL_TILE_W        (32U)
#endif
#ifndef LCD_EGL_TILE_H
  #define LCD_EGL_TILE_H        (32U)
#endif
#ifndef LCD_EGL_DEPTH_UINT16
  #define LCD_EGL_DEPTH_UINT16  (1)
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "lcd.h"
#ifdef __cplusplus
}
#endif

/* EmberGL 自身有几个 -Wall 提示，屏蔽掉 */
#ifdef __cplusplus
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wint-in-bool-context"
  #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "egl_device.h"
#ifdef __cplusplus
  #pragma GCC diagnostic pop
#endif

#define LCD_EGL_RGB565(r5, g6, b5)  ((uint16_t)((((r5) & 0x1FU) << 11) | \
                                                 (((g6) & 0x3FU) << 5)  | \
                                                  ((b5) & 0x1FU)))

class graphics_device_lcd : public egl::graphics_device<graphics_device_lcd>
{
public:
  enum {supports_dma = false};       /* EmberGL 的 DMA 通道未启用 */
  enum {max_concurrency = 1};
  enum {fb_width  = LCD_EGL_WIDTH};
  enum {fb_height = LCD_EGL_HEIGHT};
  enum {fb_format = egl::pixfmt_r5g6b5};   /* 16 位并口 = 原生 RGB565，不换字节 */
  typedef egl::pixel<egl::e_pixel_format(fb_format)> fb_format_t;

  graphics_device_lcd();
  void init_rasterizer(const egl::rasterizer_cfg&,
                       const egl::rasterizer_tiling_cfg&,
                       const egl::rasterizer_vertex_cache_cfg&);
  void init_dma(egl::rasterizer_data_transfer*, uint8_t, void*, size_t);

  template<class IPShader>
  void fast_draw_hline(uint16_t x_, uint16_t y_, uint16_t width_, const IPShader&);
  template<class IPShader>
  void fast_draw_vline(uint16_t x_, uint16_t y_, uint16_t height_, const IPShader&);
  template<class IPShader>
  void fast_draw_rect(uint16_t x_, uint16_t y_, uint16_t width_, uint16_t height_,
                      const IPShader&);

  uint32_t pushed_pixels() const { return m_pushed_px; }
  void reset_pushed_pixels()     { m_pushed_px = 0U; }

private:
  graphics_device_lcd(const graphics_device_lcd&);
  void operator=(const graphics_device_lcd&);

  virtual void submit_tile(uint8_t tx_, uint8_t ty_,
                           const egl::vec2u16 &reg_min_, const egl::vec2u16 &reg_end_,
                           uint16_t thread_idx_);

  fb_format_t *m_tile_rt0;
  egl::rasterizer_tile_size_t m_tile_width;
  egl::rasterizer_tile_size_t m_tile_height;
  uint32_t m_pushed_px;
};

struct lcd_embergl_mem_cfg : public egl::rasterizer_memory_cfg_base
{
  enum {depth_format = LCD_EGL_DEPTH_UINT16 ? egl::depthfmt_uint16 : egl::depthfmt_float32};
  enum {depth_hiz    = false};
  enum {rt0_fmt = egl::pixfmt_r5g6b5};
  enum {rt1_fmt = egl::pixfmt_none};
  enum {rt2_fmt = egl::pixfmt_none};
  enum {rt3_fmt = egl::pixfmt_none};
  enum {tile_width  = LCD_EGL_TILE_W};
  enum {tile_height = LCD_EGL_TILE_H};
  enum {tile_order  = egl::tileorder_linear};
  enum {max_dispatches = 16};
  enum {pso_store_size = 4096};
  enum {max_pso_state_size = 256};
  enum {max_clusters = 256};
  enum {max_cluster_strips = 128};
  enum {num_strip_clusters = 16};
  enum {max_ptv_buffer_size = 2048};
  enum {vcache_size = 16384};
  enum {max_dma_transfers = 0};
};

typedef egl::rasterizer_memory<graphics_device_lcd, lcd_embergl_mem_cfg> lcd_embergl_mem_t;

#ifdef __cplusplus
extern "C" {
#endif
int      LCD_EGL_Init(void);
void     LCD_EGL_BeginFrame(uint16_t rgb565_clear_color);
void     LCD_EGL_BeginFramePartial(uint16_t rgb565_clear_color,
                                   uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h);
void     LCD_EGL_BeginFrameBuffered(uint16_t rgb565_clear_color,
                                    uint16_t y0, uint16_t band_h);
void     LCD_EGL_PresentBand(void);
void     LCD_EGL_EndFrame(void);
uint32_t LCD_EGL_PushedPixels(void);
#ifdef __cplusplus
}
graphics_device_lcd &LCD_EGL_Device(void);
lcd_embergl_mem_t   &LCD_EGL_Memory(void);
#endif

/* 立即模式绘制。分辨率已由基类裁剪过，这里直接开窗写。 */
template<class IPShader>
void graphics_device_lcd::fast_draw_hline(uint16_t x_, uint16_t y_,
                                          uint16_t width_, const IPShader &ips_)
{
  uint16_t x_end = (uint16_t)(x_ + width_);
  fb_format_t res;

  LCD_SetWindow(x_, y_, (uint16_t)(x_end - 1U), y_);
  do
  {
    ips_.exec(res, x_, y_);
    LCD_WriteData(res.v);
  } while (++x_ < x_end);
}

template<class IPShader>
void graphics_device_lcd::fast_draw_vline(uint16_t x_, uint16_t y_,
                                          uint16_t height_, const IPShader &ips_)
{
  uint16_t y_end = (uint16_t)(y_ + height_);
  fb_format_t res;

  LCD_SetWindow(x_, y_, x_, (uint16_t)(y_end - 1U));
  do
  {
    ips_.exec(res, x_, y_);
    LCD_WriteData(res.v);
  } while (++y_ < y_end);
}

template<class IPShader>
void graphics_device_lcd::fast_draw_rect(uint16_t x_, uint16_t y_,
                                         uint16_t width_, uint16_t height_,
                                         const IPShader &ips_)
{
  uint16_t x_end = (uint16_t)(x_ + width_);
  uint16_t y_end = (uint16_t)(y_ + height_);
  fb_format_t res;

  LCD_SetWindow(x_, y_, (uint16_t)(x_end - 1U), (uint16_t)(y_end - 1U));
  do
  {
    uint16_t x = x_;
    do
    {
      ips_.exec(res, x, y_);
      LCD_WriteData(res.v);
    } while (++x < x_end);
  } while (++y_ < y_end);
}

#endif /* LCD_PORT_EMBERGL */
#endif /* __LCD_PORT_EMBERGL_H */
