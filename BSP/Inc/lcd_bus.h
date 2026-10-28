/* FMC 8080 并口总线（寄存器级；ST 没有给 H7 的 FMC 提供 LL 驱动） */
#ifndef __LCD_BUS_H
#define __LCD_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lcd_conf.h"

void LCD_Bus_Init(void);      /* 配引脚 + FMC 寄存器 + MPU 区域 */
void LCD_Bus_DeInit(void);

/* 运行时可调时序（刷屏速度旋钮，见 lcd_conf.h 的 WR_DATAST 说明） */
void     LCD_Bus_SetWriteTiming(uint32_t datast);
uint32_t LCD_Bus_GetWriteTiming(void);
void     LCD_Bus_SetReadTiming(uint32_t addset, uint32_t datast);

static inline void LCD_WriteCmd(uint16_t cmd)
{
  *((volatile uint16_t *)LCD_FMC_CMD_ADDR) = cmd;
}

static inline void LCD_WriteData(uint16_t data)
{
  *((volatile uint16_t *)LCD_FMC_DATA_ADDR) = data;
}

static inline uint16_t LCD_ReadData(void)
{
  return *((volatile uint16_t *)LCD_FMC_DATA_ADDR);
}

static inline void LCD_WriteReg(uint16_t reg, uint16_t val)
{
  LCD_WriteCmd(reg);
  LCD_WriteData(val);
}

uint16_t LCD_ReadReg(uint16_t reg);
void     LCD_WriteDataRepeat(uint32_t count, uint16_t data);
void     LCD_WriteDataBlock(const uint16_t *buf, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_BUS_H */
