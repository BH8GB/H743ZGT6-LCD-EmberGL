/* DMA 异步刷屏。LCD_USE_DMA=0 时全部退化为同步 BlitArea。
 *
 * 用 MDMA 而不是 DMA1/DMA2：ST 官方 H743I-EVAL BSP 里 SDRAM(FMC) 的搬运用的就是
 * MDMA_Channel0（见 stm32h743i_eval_sdram.h 的 SDRAM_MDMAx_CHANNEL）。MDMA 在 D1 域、
 * AXI 主设备，离 FMC 最近。
 *
 * 数据流：源缓冲(地址递增) → GRAM 数据口(地址固定)。窗口由 CPU 先开好。
 * ⚠ LCD_USE_DMA=1 的路径未在真实硬件上验证过，上板请按 README 的 bring-up 步骤确认。
 */
#include "lcd_dma.h"
#include "lcd.h"

#if (LCD_USE_DMA == 0)

void LCD_DMA_Init(void) {}

void LCD_DMA_BlitStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *src, uint16_t pitch)
{
  LCD_BlitArea(x, y, w, h, src, pitch);
}

uint8_t LCD_DMA_IsBusy(void) { return 0U; }
void    LCD_DMA_Wait(void)   {}
uint8_t LCD_DMA_GetError(void) { return 0U; }

#else

#define LCD_DMA_CH            MDMA_Channel0

/* MDMA 的 CTCR 字段与 DMA 不同：SINC/DINC 00=固定, 10=递增, 11=递减；
 * 递增步长由 SINCOS/DINCOS 决定。下面与 ST HAL 的 MDMA 宏编码一致。 */
#define LCD_MDMA_SINC_INC_HW  (MDMA_CTCR_SINC_1 | MDMA_CTCR_SINCOS_0)  /* 源按半字递增 */
#define LCD_MDMA_DINC_FIXED   (0U)                                      /* 目标固定 */
#define LCD_MDMA_SSIZE_HW     (MDMA_CTCR_SSIZE_0)                       /* 源 16 位 */
#define LCD_MDMA_DSIZE_HW     (MDMA_CTCR_DSIZE_0)                       /* 目标 16 位 */

static volatile uint8_t lcd_dma_error  = 0U;
static volatile uint8_t lcd_dma_active = 0U;

/* DTCM(0x2000_0000)/ITCM 只有 CPU 能访问，MDMA 会出错 */
static uint8_t lcd_dma_is_cpu_only_ram(const void *p)
{
  uint32_t a = (uint32_t)p;

  if ((a >= 0x20000000UL) && (a < 0x20020000UL)) return 1U;
  if (a < 0x00010000UL)                          return 1U;
  return 0U;
}

void LCD_DMA_Init(void)
{
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_MDMA);

  LCD_DMA_CH->CCR = 0U;
  while ((LCD_DMA_CH->CCR & MDMA_CCR_EN) != 0U) {}
  LCD_DMA_CH->CIFCR = MDMA_CISR_CTCIF | MDMA_CISR_TEIF;

  lcd_dma_error  = 0U;
  lcd_dma_active = 0U;
}

void LCD_DMA_BlitStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *src, uint16_t pitch)
{
  uint32_t pixels;

  if ((src == 0) || (w == 0U) || (h == 0U)) return;
  if (pitch == 0U) pitch = w;

  /* 只有连续缓冲能一次搬完；源在 DTCM 时也退回同步 */
  if ((pitch != w) || (lcd_dma_is_cpu_only_ram(src) != 0U))
  {
    if (pitch == w) lcd_dma_error = 1U;
    LCD_BlitArea(x, y, w, h, src, pitch);
    return;
  }

  LCD_DMA_Wait();
  LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));

  LCD_DMA_CH->CCR = 0U;
  while ((LCD_DMA_CH->CCR & MDMA_CCR_EN) != 0U) {}
  LCD_DMA_CH->CIFCR = MDMA_CISR_CTCIF | MDMA_CISR_TEIF;

  pixels = (uint32_t)w * (uint32_t)h;

  LCD_DMA_CH->CSAR   = (uint32_t)src;
  LCD_DMA_CH->CDAR   = (uint32_t)LCD_FMC_DATA_ADDR;
  LCD_DMA_CH->CBNDTR = (pixels * 2U) & MDMA_CBNDTR_BNDT;   /* 单位是字节 */

  LCD_DMA_CH->CTCR = LCD_MDMA_SINC_INC_HW | LCD_MDMA_DINC_FIXED |
                     LCD_MDMA_SSIZE_HW | LCD_MDMA_DSIZE_HW |
                     (1U << MDMA_CTCR_SWRM_Pos);           /* 软件请求模式 */

  SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)src, (int32_t)(pixels * 2U));

  LCD_DMA_CH->CCR = MDMA_CCR_EN | MDMA_CCR_SWRQ;
  lcd_dma_active  = 1U;
}

uint8_t LCD_DMA_IsBusy(void)
{
  if (lcd_dma_active == 0U) return 0U;
  if ((LCD_DMA_CH->CISR & (MDMA_CISR_CTCIF | MDMA_CISR_TEIF)) != 0U) return 0U;
  return 1U;
}

void LCD_DMA_Wait(void)
{
  if (lcd_dma_active == 0U) return;

  while ((LCD_DMA_CH->CISR & (MDMA_CISR_CTCIF | MDMA_CISR_TEIF)) == 0U) {}

  if ((LCD_DMA_CH->CISR & MDMA_CISR_TEIF) != 0U) lcd_dma_error = 1U;

  LCD_DMA_CH->CIFCR = MDMA_CISR_CTCIF | MDMA_CISR_TEIF;
  lcd_dma_active = 0U;
}

uint8_t LCD_DMA_GetError(void) { return lcd_dma_error; }

#endif /* LCD_USE_DMA */
