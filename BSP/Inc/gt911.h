/* GT911 电容触摸（硬件 I2C1，可切软件 I2C） */
#ifndef __GT911_H
#define __GT911_H

#include "lcd_conf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint8_t  id;
  uint16_t x;
  uint16_t y;
  uint16_t size;
} ct_point_t;

uint8_t           GT911_Init(void);            /* 0 = 成功 */
uint8_t           GT911_Scan(void);            /* 返回当前触摸点数 */
uint8_t           GT911_IsTouched(uint16_t *x, uint16_t *y);
const ct_point_t *GT911_GetPoints(void);
const char       *GT911_GetProductID(void);    /* 4 字节，如 "911" */
uint8_t           GT911_GetConfigVersion(void);
void              GT911_GetResolution(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif /* __GT911_H */
