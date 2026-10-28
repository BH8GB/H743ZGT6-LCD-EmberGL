/* LVGL v8/v9 显示 + 触摸适配。
 * LVGL 的脏区域缓冲是整块连续的，所以刷屏直接走 LCD_BlitArea 的最快路径。 */
#include "lcd_conf.h"

#if LCD_PORT_LVGL

#include "lcd_port_lvgl.h"
#include "lcd.h"
#include "gt911.h"

#define LCD_LVGL_BUF_PIXELS  ((uint32_t)LCD_PANEL_PORTRAIT_H * LCD_LVGL_BUF_LINES)

static lv_disp_draw_buf_t lvgl_draw_buf;                 /* v8 */
static LCD_BUF_ATTR lv_color_t lvgl_buf1[LCD_LVGL_BUF_PIXELS];
#if (LCD_LVGL_USE_DOUBLE_BUF == 1)
static LCD_BUF_ATTR lv_color_t lvgl_buf2[LCD_LVGL_BUF_PIXELS];
#endif

static volatile uint32_t lvgl_tick_ms = 0U;

void lv_port_tick_inc(void)
{
  lvgl_tick_ms++;
#if (LVGL_VERSION_MAJOR < 9)
  lv_tick_inc(1);                       /* v8 由 LVGL 自己维护 tick */
#endif
}

uint32_t lv_port_tick_get(void) { return lvgl_tick_ms; }

/* ---- flush ---- */
#if (LVGL_VERSION_MAJOR >= 9)

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
  uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

  LCD_BlitArea((uint16_t)area->x1, (uint16_t)area->y1, w, h,
               (const uint16_t *)px_map, w);
  lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
  lv_display_t *disp = lv_display_create(lcddev.width, lcddev.height);

#if (LCD_LVGL_USE_DOUBLE_BUF == 1)
  lv_display_set_buffers(disp, lvgl_buf1, lvgl_buf2,
                         sizeof(lvgl_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
  lv_display_set_buffers(disp, lvgl_buf1, NULL,
                         sizeof(lvgl_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
  lv_display_set_flush_cb(disp, lvgl_flush_cb);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
}

#else  /* v8 */

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
  uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

  LCD_BlitArea((uint16_t)area->x1, (uint16_t)area->y1, w, h,
               (const uint16_t *)color_p, w);
  lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
  static lv_disp_drv_t disp_drv;

  lv_disp_draw_buf_init(&lvgl_draw_buf, lvgl_buf1,
#if (LCD_LVGL_USE_DOUBLE_BUF == 1)
                        lvgl_buf2,
#else
                        NULL,
#endif
                        LCD_LVGL_BUF_PIXELS);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = lcddev.width;
  disp_drv.ver_res  = lcddev.height;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &lvgl_draw_buf;
  lv_disp_drv_register(&disp_drv);
}

#endif /* LVGL_VERSION_MAJOR */

/* ---- 触摸输入 ---- */
static void lvgl_touch_read_cb(
#if (LVGL_VERSION_MAJOR >= 9)
    lv_indev_t *indev,
#else
    lv_indev_drv_t *indev,
#endif
    lv_indev_data_t *data)
{
  uint16_t x, y;

  (void)indev;

  if (GT911_IsTouched(&x, &y) != 0U)
  {
    data->point.x = (lv_coord_t)LCD_TOUCH_MAP_X(x, y);
    data->point.y = (lv_coord_t)LCD_TOUCH_MAP_Y(x, y);
    data->state   = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void lv_port_indev_init(void)
{
#if (LVGL_VERSION_MAJOR >= 9)
  lv_indev_t *indev = lv_indev_create();

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
#else
  static lv_indev_drv_t indev_drv;

  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_read_cb;
  lv_indev_drv_register(&indev_drv);
#endif
}

#endif /* LCD_PORT_LVGL */
