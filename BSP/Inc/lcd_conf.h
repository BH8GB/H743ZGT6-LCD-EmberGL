/* 板级配置。所有宏都可用 -D 覆盖。
 * LCD: FMC 8080 16 位并口 | 触摸: GT911 @ I2C1 | 背光: GPIO
 * FMC 完全由驱动接管（引脚/时钟/寄存器/MPU），CubeMX 里不需要配置 FMC。 */
#ifndef __LCD_CONF_H
#define __LCD_CONF_H

#ifndef LCD_PORT_NO_CUBEMX
  #define LCD_PORT_NO_CUBEMX      (0)     /* 1 = 非 CubeMX 工程，自行定义引脚 */
#endif

#if !LCD_PORT_NO_CUBEMX
  #include "main.h"
#else
  #include "stm32h7xx.h"
  #include "stm32h7xx_ll_gpio.h"
  #include "stm32h7xx_ll_bus.h"
  #include "stm32h7xx_ll_utils.h"
#endif

#include <stdint.h>

#ifndef LCD_USE_TEXT
  #define LCD_USE_TEXT            (1)     /* 0 = 不编译字符显示，省约 20KB Flash */
#endif
#ifndef LCD_USE_CANVAS
  #define LCD_USE_CANVAS          (1)
#endif
#ifndef LCD_CANVAS_LINES
  #define LCD_CANVAS_LINES        (32U)   /* 画布行数，RAM = 屏宽 x 行数 x 2B */
#endif
#ifndef LCD_USE_DMA
  #define LCD_USE_DMA             (1)     /* 1 = MDMA 异步刷屏 */
#endif
#ifndef LCD_PORT_LVGL
  #define LCD_PORT_LVGL           (0)
#endif
#ifndef LCD_PORT_EMBERGL
  #define LCD_PORT_EMBERGL        (0)
#endif
#ifndef LCD_LVGL_BUF_LINES
  #define LCD_LVGL_BUF_LINES      (40U)
#endif
#ifndef LCD_LVGL_USE_DOUBLE_BUF
  #define LCD_LVGL_USE_DOUBLE_BUF (0)
#endif

/* 大块缓冲(画布/LVGL)的存放位置。默认空 = 落 .bss(DTCM)，不占 Flash。
 * 要用 DMA 时必须放到 AXI SRAM/D2 SRAM（DTCM 不可被 DMA 访问），做法见
 * BSP/README.md（CubeMX 会重写链接脚本，所以别手改 .ld）。 */
/* 大块缓冲默认放 AXI SRAM：MDMA/DMA 可达，且不占 DTCM。
   DTCM(0x2000_0000) 只有 CPU 能访问，用作 DMA 源会传输出错。
   用绝对地址段，避免依赖会被 CubeMX 重写的链接脚本。 */
#ifndef LCD_BUF_ATTR
  #define LCD_BUF_ATTR            __attribute__((aligned(32)))
#endif

/* ---- 面板几何：竖屏物理分辨率 480x800，横屏由 MADCTL.MV 交换得到 ---- */
#ifndef LCD_PANEL_PORTRAIT_W
  #define LCD_PANEL_PORTRAIT_W    (480U)
#endif
#ifndef LCD_PANEL_PORTRAIT_H
  #define LCD_PANEL_PORTRAIT_H    (800U)
#endif
#ifndef LCD_DEFAULT_DIR
  #define LCD_DEFAULT_DIR         (1)     /* 0/2=竖屏 1/3=横屏(1 为默认横屏) */
#endif

/* ---- FMC 8080 总线 ---- */
#ifndef LCD_FMC_BANK_BASE
  #define LCD_FMC_BANK_BASE       (0x6C000000UL)  /* Bank1 NE4；NE1=0x60000000 */
#endif
#ifndef LCD_FMC_RS_LINE
  #define LCD_FMC_RS_LINE         (6U)    /* RS 接 FMC_A6 */
#endif
/* 16 位总线时 FMC_A[n] 对应 HADDR[n+1]。
   A6 -> HADDR[7] = 0x80。命令地址必须落在 A6=0 的 16 位槽内，
   正点原子例程用 0x6C000000|0x7E（基址 + 0x7E）而不是裸基址：
   裸基址在 32 位/缓存访问下可能被当成对齐整体，丢掉低位槽位。 */
#define LCD_FMC_RS_OFFSET         (1UL << (LCD_FMC_RS_LINE + 1U))
#define LCD_FMC_CMD_ADDR          (LCD_FMC_BANK_BASE | (LCD_FMC_RS_OFFSET - 2UL))
#define LCD_FMC_DATA_ADDR         (LCD_FMC_BANK_BASE | LCD_FMC_RS_OFFSET)

#ifndef LCD_FMC_CLK_HZ
  #define LCD_FMC_CLK_HZ          (240000000UL)
#endif

#ifndef LCD_FMC_RD_ADDSET
  #define LCD_FMC_RD_ADDSET       (15U)
#endif
#ifndef LCD_FMC_RD_ADDHLD
  #define LCD_FMC_RD_ADDHLD       (15U)
#endif
#ifndef LCD_FMC_RD_DATAST
  #define LCD_FMC_RD_DATAST       (60U)
#endif
/* 写时序 = (ADDSET+1)+(DATAST+1) 个 HCLK。
 * 实测（NT35510 + 正点原子 4.3" 800x480，FMC 240MHz）：
 *   DATAST=12 -> 66ns -> 42fps
 *   DATAST=8  -> 50ns -> 57fps
 *   DATAST=6  -> 41ns -> 69fps
 *   DATAST=5  -> 37ns -> 78fps  <= 当前
 * 再往下接近 NT35510 写脉冲下限(~25ns)，并口信号完整性先崩，出现零散花点。 */
#ifndef LCD_FMC_WR_ADDSET
  #define LCD_FMC_WR_ADDSET       (2U)
#endif
#ifndef LCD_FMC_WR_ADDHLD
  #define LCD_FMC_WR_ADDHLD       (1U)
#endif
#ifndef LCD_FMC_WR_DATAST
  #define LCD_FMC_WR_DATAST       (5U)    /* 37ns @240MHz -> 78fps。花屏就退回 6 */
#endif

/* CubeMX/HAL 已配置 FMC 时保持 0；无 HAL 的独立移植时置 1 由驱动接管 */
#ifndef LCD_BUS_INIT_GPIO
  #define LCD_BUS_INIT_GPIO       (0)
#endif
#ifndef LCD_BUS_INIT_FMC
  #define LCD_BUS_INIT_FMC        (0)
#endif

/* 面板初始化/复位用的毫秒延时。
   CubeMX 的 HAL 时间基准可能不是 SysTick（或已被 FreeRTOS 接管），
   此时 LL_mDelay 会死在 COUNTFLAG 轮询上，所以不能直接用 LL_mDelay。
   调度器未启动（LCD_Init 在 osKernelStart 之前）必须用 HAL_Delay，
   启动之后交给 osDelay，把 CPU 让给其他任务。 */
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
#ifndef LCD_DELAY_MS
  #define LCD_DELAY_MS(ms)                                                \
    do {                                                                  \
      if (osKernelGetState() == osKernelRunning) { osDelay((ms)); }        \
      else                                       { HAL_Delay((ms)); }      \
    } while (0)
#endif
#ifndef LCD_FMC_GPIOD_PINS
  #define LCD_FMC_GPIOD_PINS      (LL_GPIO_PIN_0|LL_GPIO_PIN_1|LL_GPIO_PIN_4|LL_GPIO_PIN_5 | \
                                   LL_GPIO_PIN_8|LL_GPIO_PIN_9|LL_GPIO_PIN_10| \
                                   LL_GPIO_PIN_14|LL_GPIO_PIN_15)
#endif
#ifndef LCD_FMC_GPIOE_PINS
  #define LCD_FMC_GPIOE_PINS      (LL_GPIO_PIN_7|LL_GPIO_PIN_8|LL_GPIO_PIN_9|LL_GPIO_PIN_10| \
                                   LL_GPIO_PIN_11|LL_GPIO_PIN_12|LL_GPIO_PIN_13| \
                                   LL_GPIO_PIN_14|LL_GPIO_PIN_15)
#endif
#ifndef LCD_FMC_GPIOF_PINS
  #define LCD_FMC_GPIOF_PINS      (LL_GPIO_PIN_12)      /* PF12 = FMC_A6 = RS */
#endif
#ifndef LCD_FMC_GPIOG_PINS
  #define LCD_FMC_GPIOG_PINS      (LL_GPIO_PIN_12)      /* PG12 = FMC_NE4 */
#endif

/* ---- 面板参数 ---- */
#ifndef LCD_CONTROLLER_NT35510
  #define LCD_CONTROLLER_NT35510  (1)     /* 1=NT35510（正点原子 4.3" MCU 屏）；0=ILI9806G */
#endif
#ifndef LCD_ILI9806_PANEL_RES
  #define LCD_ILI9806_PANEL_RES   (0x81U)   /* ILI9806G 专用：0xF7 分辨率 */
#endif
#ifndef LCD_MADCTL_DIR0
  #define LCD_MADCTL_DIR0         (0x00U)   /* 竖屏 */
#endif
#ifndef LCD_MADCTL_DIR1
  #define LCD_MADCTL_DIR1         (0xA0U)   /* 横屏 MY|MV */
#endif
#ifndef LCD_MADCTL_DIR2
  #define LCD_MADCTL_DIR2         (0xC0U)   /* 竖屏翻转 */
#endif
#ifndef LCD_MADCTL_DIR3
  #define LCD_MADCTL_DIR3         (0x60U)   /* 横屏翻转 */
#endif
#ifndef LCD_ILI9806_BGR
  #define LCD_ILI9806_BGR         (0x08U)   /* 红蓝颠倒就改 0 */
#endif

/* ---- 背光 / 复位 / 引脚（CubeMX 工程由 main.h 提供，否则用下面的默认值）---- */
#ifndef LCD_BL_ACTIVE_LEVEL
  #define LCD_BL_ACTIVE_LEVEL     (1)       /* 1 = 高电平点亮 */
#endif
#ifndef LCD_USE_RST_PIN
  #define LCD_USE_RST_PIN         (1)       /* RST 接 NRST 时置 0 */
#endif
#ifndef LCD_BL_Pin
  #define LCD_BL_Pin              LL_GPIO_PIN_15
  #define LCD_BL_GPIO_Port        GPIOB
#endif
#ifndef LCD_RST_Pin
  #define LCD_RST_Pin             LL_GPIO_PIN_14
  #define LCD_RST_GPIO_Port       GPIOB
#endif
#ifndef CT_RST_Pin
  #define CT_RST_Pin              LL_GPIO_PIN_0
  #define CT_RST_GPIO_Port        GPIOB
#endif
#ifndef CT_INT_Pin
  #define CT_INT_Pin              LL_GPIO_PIN_13
  #define CT_INT_GPIO_Port        GPIOC
#endif

/* ---- GT911 触摸 ---- */
#ifndef CT_I2C_TIMEOUT_LOOPS
  #define CT_I2C_TIMEOUT_LOOPS    (200000U)
#endif
#ifndef CT_GT911_ADDR
  #define CT_GT911_ADDR           (0x5DU)   /* 复位时 INT 拉低 */
#endif
#ifndef CT_GT911_ADDR_ALT
  #define CT_GT911_ADDR_ALT       (0x14U)   /* 复位时 INT 拉高 */
#endif
#ifndef CT_MAX_TOUCH
  #define CT_MAX_TOUCH            (5U)
#endif
#ifndef CT_SWAP_XY
  #define CT_SWAP_XY              (0)       /* 芯片上报方向与显示不一致时置 1 */
#endif
#ifndef LCD_TOUCH_MAP_X
  #define LCD_TOUCH_MAP_X(x, y)   (x)       /* 旋转/镜像时在这里换算 */
#endif
#ifndef LCD_TOUCH_MAP_Y
  #define LCD_TOUCH_MAP_Y(x, y)   (y)
#endif

/* 备用：触摸不在硬件 I2C 引脚上时用软件模拟 I2C（需把那两个脚配成开漏输出） */
#ifndef CT_USE_SOFT_I2C
  #define CT_USE_SOFT_I2C         (0)
#endif
#if CT_USE_SOFT_I2C
  #ifndef CT_I2C_DELAY_US
    #define CT_I2C_DELAY_US       (2U)
  #endif
  #ifndef CT_SOFT_SCL_PORT
    #define CT_SOFT_SCL_PORT      GPIOB
    #define CT_SOFT_SCL_PIN       LL_GPIO_PIN_1
    #define CT_SOFT_SDA_PORT      GPIOB
    #define CT_SOFT_SDA_PIN       LL_GPIO_PIN_2
  #endif
#endif

#endif /* __LCD_CONF_H */
