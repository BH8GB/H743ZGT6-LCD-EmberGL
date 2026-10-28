/* 屏幕自检：ID、色条、渐变、文字、整屏填充 FPS、触摸打点 */
#ifndef __LCD_SELFTEST_H
#define __LCD_SELFTEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void LCD_SelfTest(void);
void LCD_TouchInit(void);
void LCD_TouchLoop(uint32_t delay_ms);
void LCD_BitTest(void);
void LCD_GeoTest(void);
void LCD_FontTest(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_SELFTEST_H */
