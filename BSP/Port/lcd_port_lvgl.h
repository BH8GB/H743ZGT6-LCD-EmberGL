/* LVGL v8/v9 适配。
 * 用法: lcd_conf.h 里 LCD_PORT_LVGL=1 → lv_init() 后调 lv_port_disp_init() /
 *       lv_port_indev_init()，并在 SysTick 中断里调 lv_port_tick_inc()。
 * lv_conf.h: LV_COLOR_DEPTH=16, LV_COLOR_16_SWAP=0（16 位并口不需要换字节）。 */
#ifndef __LCD_PORT_LVGL_H
#define __LCD_PORT_LVGL_H

#include "lcd_conf.h"

#if LCD_PORT_LVGL

#include "lvgl.h"

void     lv_port_disp_init(void);
void     lv_port_indev_init(void);
void     lv_port_tick_inc(void);      /* 1ms 调一次 */
uint32_t lv_port_tick_get(void);

#endif /* LCD_PORT_LVGL */
#endif /* __LCD_PORT_LVGL_H */
