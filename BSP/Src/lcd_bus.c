/* FMC 8080 并口总线。CubeMX/HAL 负责 FMC 基配置（引脚/时钟/BCR），
 * 本文件只接管 MPU Region1 与读写时序覆盖，便于运行时调优。
 * 无 HAL 的独立移植：把 LCD_BUS_INIT_FMC / LCD_BUS_INIT_GPIO 置 1。 */
#include "lcd_bus.h"

#define FMC_Bank1                  ((FMC_Bank1_TypeDef  *) FMC_Bank1_R_BASE)
#define FMC_Bank1E                 ((FMC_Bank1E_TypeDef *) FMC_Bank1E_R_BASE)

/* NE4 -> BTCR[6]=BCR4, BTCR[7]=BTR4, BWTR[6]=BWTR4（换 bank 时改这三个索引） */
#define LCD_FMC_BTCR_BCR           FMC_Bank1->BTCR[6]
#define LCD_FMC_BTCR_BTR           FMC_Bank1->BTCR[7]
#define LCD_FMC_BWTR               FMC_Bank1E->BWTR[6]

#define BCR_MBKEN                  (1UL << 0)
#define BCR_MTYP_SRAM              (0UL << FMC_BCRx_MTYP_Pos)
#define BCR_MWID_16BIT             (1UL << FMC_BCRx_MWID_Pos)
#define BCR_FACCEN                 (1UL << 6)
#define BCR_WREN                   (1UL << FMC_BCRx_WREN_Pos)
#define BCR_EXTMOD                 (1UL << FMC_BCRx_EXTMOD_Pos)

#define TR_ADDSET(v)               (((uint32_t)(v) & 0x0FUL) << FMC_BTRx_ADDSET_Pos)
#define TR_ADDHLD(v)               (((uint32_t)(v) & 0x0FUL) << FMC_BTRx_ADDHLD_Pos)
#define TR_DATAST(v)               (((uint32_t)(v) & 0xFFUL) << FMC_BTRx_DATAST_Pos)
#define TR_ACCMOD_A                (0UL << FMC_BTRx_ACCMOD_Pos)

static uint8_t lcd_bus_initialized = 0;

/* FMC 引脚配成 AF12。自己配而不依赖 CubeMX 的 HAL_FMC_MspInit()，
 * 因为那个函数只有 HAL 的 FMC 初始化才会调用，用 LL 时永远不会执行。 */
#if LCD_BUS_INIT_GPIO
static void LCD_Bus_GpioInit(void)
{
  LL_GPIO_InitTypeDef gpio = {0};

  LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOD |
                           LL_AHB4_GRP1_PERIPH_GPIOE |
                           LL_AHB4_GRP1_PERIPH_GPIOF |
                           LL_AHB4_GRP1_PERIPH_GPIOG);

  gpio.Mode       = LL_GPIO_MODE_ALTERNATE;
  gpio.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  gpio.Pull       = LL_GPIO_PULL_NO;
  gpio.Alternate  = LL_GPIO_AF_12;

  gpio.Pin = LCD_FMC_GPIOD_PINS; LL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = LCD_FMC_GPIOE_PINS; LL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = LCD_FMC_GPIOF_PINS; LL_GPIO_Init(GPIOF, &gpio);
  gpio.Pin = LCD_FMC_GPIOG_PINS; LL_GPIO_Init(GPIOG, &gpio);
}
#endif

/* MPU 布局（编号小者优先命中）：
 *   Region0: 整个 4GB，Normal / 非缓存 / 可共享 / 全权限 / 可取指
 *   Region1: FMC 256MB，Device / 非缓存（覆盖 Region0，符合 NOR/SRAM 语义）
 * CubeMX 生成的 MPU_Config() 把 4GB 设成 no-access，会让 AXI SRAM(0x24000000)
 * 上的 EmberGL 画布不可访问，表现为画完自检后黑屏。
 *
 * 注意 ARMv7-M 属性编码：TEX=1,C=0,B=0 是 Device（禁止取指），
 * TEX=1,C=0,B=1 才是 Normal 非缓存。这里用后者，否则整片 Flash 无法执行。 */
static void LCD_Bus_MpuConfig(void)
{
  LL_MPU_Disable();

  LL_MPU_ConfigRegion(LL_MPU_REGION_NUMBER0,
                      0x00U,
                      0x00000000UL,
                      LL_MPU_REGION_SIZE_4GB |
                      LL_MPU_TEX_LEVEL1 |
                      LL_MPU_ACCESS_NOT_CACHEABLE |
                      LL_MPU_ACCESS_BUFFERABLE |
                      LL_MPU_ACCESS_SHAREABLE |
                      LL_MPU_REGION_FULL_ACCESS |
                      LL_MPU_INSTRUCTION_ACCESS_ENABLE);

  LL_MPU_ConfigRegion(LL_MPU_REGION_NUMBER1,
                      0x00U,
                      LCD_FMC_BANK_BASE,
                      LL_MPU_REGION_SIZE_256MB |
                      LL_MPU_TEX_LEVEL0 |
                      LL_MPU_ACCESS_NOT_CACHEABLE |
                      LL_MPU_ACCESS_NOT_BUFFERABLE |
                      LL_MPU_ACCESS_NOT_SHAREABLE |
                      LL_MPU_REGION_FULL_ACCESS |
                      LL_MPU_INSTRUCTION_ACCESS_DISABLE);
  LL_MPU_Enable(LL_MPU_CTRL_PRIVILEGED_DEFAULT);
  __DSB();
  __ISB();
}

void LCD_Bus_Init(void)
{
  if (lcd_bus_initialized) return;
  lcd_bus_initialized = 1;

#if LCD_BUS_INIT_GPIO
  LCD_Bus_GpioInit();
#endif

  LCD_Bus_MpuConfig();

#if LCD_BUS_INIT_FMC
  LL_RCC_SetFMCClockSource(LL_RCC_FMC_CLKSOURCE_HCLK);   /* FMC 在 AHB3/D1 域 */
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_FMC);

  /* BCR4: NOR/SRAM、16 位、写使能、扩展模式(读写时序分开) */
  LCD_FMC_BTCR_BCR = BCR_MBKEN | BCR_MTYP_SRAM | BCR_MWID_16BIT |
                     BCR_FACCEN | BCR_WREN | BCR_EXTMOD;
#endif

  LCD_Bus_SetReadTiming(LCD_FMC_RD_ADDSET, LCD_FMC_RD_DATAST);
  LCD_Bus_SetWriteTiming(LCD_FMC_WR_DATAST);
}

void LCD_Bus_SetWriteTiming(uint32_t datast)
{
  if (datast > 0xFFU) datast = 0xFFU;

  LCD_FMC_BWTR = TR_ADDSET(LCD_FMC_WR_ADDSET) |
                 TR_ADDHLD(LCD_FMC_WR_ADDHLD) |
                 TR_DATAST(datast) |
                 TR_ACCMOD_A;
}

uint32_t LCD_Bus_GetWriteTiming(void)
{
  return (LCD_FMC_BWTR & FMC_BTRx_DATAST_Msk) >> FMC_BTRx_DATAST_Pos;
}

void LCD_Bus_SetReadTiming(uint32_t addset, uint32_t datast)
{
  if (addset > 0x0FU) addset = 0x0FU;
  if (datast > 0xFFU) datast = 0xFFU;

  LCD_FMC_BTCR_BTR = TR_ADDSET(addset) |
                     TR_ADDHLD(LCD_FMC_RD_ADDHLD) |
                     TR_DATAST(datast) |
                     TR_ACCMOD_A;
}

void LCD_Bus_DeInit(void)
{
  lcd_bus_initialized = 0;

#if LCD_BUS_INIT_FMC
  LCD_FMC_BTCR_BCR = 0U;
  LCD_FMC_BTCR_BTR = 0x0FFFFFFFU;
  LCD_FMC_BWTR     = 0x0FFFFFFFU;
  LL_AHB3_GRP1_DisableClock(LL_AHB3_GRP1_PERIPH_FMC);
#endif

  LL_MPU_Disable();
  LL_MPU_DisableRegion(LL_MPU_REGION_NUMBER1);
  LL_MPU_Enable(LL_MPU_CTRL_PRIVILEGED_DEFAULT);
  __DSB();
  __ISB();
}

uint16_t LCD_ReadReg(uint16_t reg)
{
  LCD_WriteCmd(reg);
  __DSB();
  return LCD_ReadData();
}

/* 16 位写。没用 32 位写：瓶颈是 FMC 的 DATAST(每 16 位约 66ns)，而非 CPU 开销。 */
void LCD_WriteDataRepeat(uint32_t count, uint16_t data)
{
  volatile uint16_t *p = (volatile uint16_t *)LCD_FMC_DATA_ADDR;

  while (count--) *p = data;
}

void LCD_WriteDataBlock(const uint16_t *buf, uint32_t count)
{
  volatile uint16_t *p = (volatile uint16_t *)LCD_FMC_DATA_ADDR;

  while (count--) *p = *buf++;
}
