# H743ZGT6 + 4.3" 800x480 (ILI9806G) + GT911 —— LL 库驱动手册

纯 LL 实现（不依赖 HAL），CMake + arm-none-eabi-gcc，已通过 `--gc-sections` 的
条件编译设计：不用的模块不会进 Flash/RAM。

---

## 1. 硬件接口

| 模块信号 | MCU 引脚 | 说明 |
|---|---|---|
| LCD_CS   | PG12 | `FMC_NE4`（Bank1 子 bank4） |
| LCD_RS(D/C) | PF12 | `FMC_A6`，地址线当命令/数据选择 |
| LCD_WR   | PD5  | `FMC_NWE` |
| LCD_RD   | PD4  | `FMC_NOE` |
| LCD_D0~D15 | PD14,PD15,PD0,PD1,PE7~PE15,PD8,PD9,PD10 | `FMC_D0~D15` |
| LCD_RST  | PB14 | GPIO 输出（若接 NRST，把 `LCD_USE_RST_PIN` 置 0） |
| BL_CTR   | PB15 | 背光，高电平点亮（`LCD_BL_ACTIVE_LEVEL` 可反相） |
| CT_SCL   | PB8  | **I2C1_SCL**（硬件 I²C，AF4 开漏，400kHz） |
| CT_SDA   | PB9  | **I2C1_SDA** |
| CT_RST   | PB0  | GT911 复位 |
| CT_INT   | PC13 | GT911 中断 / 复位期间选 IIC 地址 |

> 线序按正点原子 4.3" 屏惯例（PG12/PF12 + PD/PE 数据总线）。
> 板子不同就改 `BSP/Inc/lcd_conf.h`，并在 CubeMX 里重指引脚（见第 7 节）。

## 2. FMC 映射

* Bank1 子 bank4，16 位总线，模式 A + 扩展模式（读写时序分开）
* 命令 `0x6C000000`（A6=0）；数据 `0x6C000080`（A6=1）
  > 16 位总线时 FMC 内部地址左移一位，`FMC_A6` 对应 `HADDR[7]`
* 写 `(2+1)+(12+1)=16` HCLK ≈ 66ns ≈ 15MHz（对齐 ILI9806G 的 tWC 最小值）
* 读 `(15+1)+(60+1)=77` HCLK ≈ 320ns

## 3. 代码结构

```
BSP/
├── Inc/
│   ├── lcd_conf.h       ★ 唯一需要改的配置文件（引脚 / 几何 / 时序 / 面板 / 编译开关）
│   ├── lcd_bus.h        FMC 8080 总线（H7 寄存器级；ST 没给 H7 的 FMC 提供 LL 驱动）
│   ├── lcd.h            面板 + 绘图 API + 通用输出原语
│   ├── lcd_canvas.h     分块画布（软件渲染器用）
│   ├── lcd_dma.h        DMA 异步刷屏（可选）
│   ├── gt911.h          电容触摸
│   └── lcd_font.h       ASCII 点阵字库
├── Src/
│   ├── lcd_bus.c        FMC 寄存器配置 + MPU 区域
│   ├── lcd_ili9806.c    ILI9806G 初始化序列
│   ├── lcd_raster.c     光栅化算法（与硬件无关，PC 上可单元测试）
│   ├── lcd.c            绘图 API + 通用输出原语
│   ├── lcd_canvas.c     分块画布实现
│   ├── lcd_dma.c        DMA 刷屏实现
│   └── gt911.c          GT911（硬件 I2C1，可切软件 I2C）
└── Port/
    ├── lcd_port_lvgl.c     LVGL v8/v9 显示 + 触摸适配（默认不参与编译）
    ├── lcd_port_embergl.h  EmberGL 适配（tile 脏矩形 → 直接推屏，无需 framebuffer）
    └── lcd_port_embergl.cpp

tests/
└── test_lcd_raster.c    光栅化算法的宿主机单元测试（见第 11 节）
```

### 编译开关（都在 `lcd_conf.h` 顶部）

| 开关 | 默认 | 作用 |
|---|---|---|
| `LCD_USE_TEXT` | 1 | 字符显示（关掉省约 20KB Flash 字库） |
| `LCD_USE_CANVAS` | 1 | 分块画布（RAM = 800×行数×2B） |
| `LCD_CANVAS_LINES` | 32 | 画布行数（32 行 = 50KB） |
| `LCD_USE_DMA` | 0 | DMA 异步刷屏 |
| `LCD_PORT_LVGL` | 0 | LVGL 适配层（需要 `lvgl.h` 在 include 路径） |
| `LCD_PORT_NO_CUBEMX` | 0 | 置 1 表示非 CubeMX 工程（自己定义引脚） |

## 4. 通用输出原语（对接图形库的入口）

不管用哪个图形库，最终都落到这几个函数上（`BSP/Inc/lcd.h`）：

```c
/* ① 刷一块像素（最常用，最快）—— LVGL flush / 脏矩形刷新 */
void LCD_BlitArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const uint16_t *src, uint16_t pitch);

/* ② 逐行/逐段渲染 —— 软件渲染器按扫描线输出 */
void LCD_DrawSpan(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void LCD_BlitSpan(uint16_t x, uint16_t y, uint16_t len, const uint16_t *src);

/* ③ 自己控窗口后连续写 —— 特殊排布/边算边写 */
void LCD_SetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
void LCD_WritePixels(const uint16_t *buf, uint32_t n);
void LCD_WritePixelRepeat(uint16_t color, uint32_t n);
```

> `pitch == w` 时 `LCD_BlitArea` 走**一次开窗 + 一次流式写**的最快路径（LVGL 的
> dirty area 缓冲就是这种）；`pitch > w` 时逐行开窗。

## 5. 图形库适配

### 5.1 LVGL（v8 / v9）

1. 把 LVGL 加入工程，让 `lvgl.h` 在 include 路径里
2. `lcd_conf.h` 里 `#define LCD_PORT_LVGL 1`
3. `lv_conf.h` 关键项：
   * `LV_COLOR_DEPTH 16`
   * **`LV_COLOR_16_SWAP 0`** ← 16 位并口不需要字节交换（只有 SPI 8 位传输才要）
4. `main()` 里：`LCD_Init(); lv_init(); lv_port_disp_init();`
5. **给 LVGL 1ms 心跳**（生成的 `SysTick_Handler` 是空的，必须自己加）：
   在 `Core/Src/stm32h7xx_it.c` 的 `USER CODE BEGIN SysTick_IRQn 1` 里加
   `lv_port_tick_inc();`（v9 也可以改用 `lv_tick_set_cb(lv_port_tick_get)`）
6. 缓冲：`lcd_port_lvgl.c` 默认给 800×40×2 = 62.5KB 的单缓冲（`PARTIAL` 模式），
   想双缓冲把 `LCD_LVGL_USE_DOUBLE_BUF` 置 1

> LVGL 不支持整屏 framebuffer 也没问题：`PARTIAL` 模式只渲染脏区域，正好匹配
> 「一次开窗 + 连续写」的最快路径。

### 5.2 EmberGL（已提供适配层，支持 C++）

`BSP/Port/lcd_port_embergl.*` 是**对着官方仓库源码写并编译验证过**的适配层
（<https://github.com/EmberGL-org/EmberGL>，已核对 `egl_device.h` 里的
`graphics_device_mem` / `submit_tile(tx,ty,reg_min,reg_end,thread_idx)` / `draw_rect`）。

EmberGL 的光栅化器**按 tile 渲染**（默认 64x64，本适配层用 32x32）：每画完一个 tile
就把该 tile 内的**脏矩形**回吐给设备。适配层把这个回吐直接接到屏上：

    submit_tile(脏矩形) → LCD_SetWindow(脏矩形) + 连续写像素

所以**不需要 768KB 整屏 framebuffer**，只要 tile 缓冲（32x32 → 颜色 2KB + 深度 2KB）。
这正是 EmberGL 官方 ILI9341 驱动用的同一套机制。

启用：`lcd_conf.h` 里 `#define LCD_PORT_EMBERGL 1`，把 `BSP/Port/lcd_port_embergl.cpp`
加进工程（需要 C++ 编译器），并把 EmberGL 的 `src/` 加进 include 路径。

### 5.3 TinyGL / 其它软件渲染器

这类库是**把像素画到一块连续内存**里的软件光栅化器（TinyGL 变体较多，本驱动不绑定
某个具体 API）。本方案没有整屏 framebuffer，所以用**分块画布**：

```c
#include "lcd_canvas.h"

LCD_CanvasInit(0);                       /* 默认 LCD_CANVAS_LINES 行 */

for (y = 0; y < LCD_GetHeight(); y += LCD_CanvasHeight())
{
    uint16_t *fb  = LCD_CanvasBuf();     /* 渲染器直接画这块内存 */
    uint16_t  cw  = LCD_CanvasWidth();   /* 行距 = cw */
    uint16_t  ch  = LCD_CanvasHeight();

    render_band(fb, cw, 0, y, cw, ch);   /* 你的渲染器：只渲染这一横带 */

    LCD_CanvasPush(0, y);                /* 整块推屏（一次开窗 + 流式写） */
    LCD_CanvasWait();                    /* 异步模式时等传完 */
}
```

要点：
* 画布是连续 RGB565，行距固定 = `LCD_CanvasWidth()`
* 渲染器只要支持"裁剪到一个横带"（scissor/clip）就能按带渲染；3D 库通常都支持
* 只画一次的场景可以直接把整屏当一条带（前提是 RAM 放得下）
* 以后外扩 SDRAM 时，把整屏 framebuffer 放进去、渲染完用 `LCD_BlitArea()` 刷脏矩形即可，
  画布层就不再必需

## 6. 内存布局 / DMA 可达性（H7 必读）

**CubeMX 生成的链接脚本把 `.data/.bss` 全放在 DTCM（0x20000000）。**

| 区域 | 地址 | 大小 | CPU | DMA1/2 | MDMA |
|---|---|---|---|---|---|
| DTCM | 0x20000000 | 128K | ✔ | ✘ | ✘ |
| AXI SRAM (D1) | 0x24000000 | 512K | ✔ | ✔ | ✔ |
| SRAM1-3 (D2) | 0x30000000 | 288K | ✔ | ✔ | ✔ |
| SRAM4 (D3) | 0x38000000 | 64K | ✔ | ✔ | ✔ |

> **DTCM 只有 CPU 能访问**，所以任何要做 DMA 源的缓冲都不能放 DTCM。
> 另外注意：**不要手改 `STM32H743xG_flash.ld`** —— CubeMX 每次生成都会重写它。

### 把大块缓冲搬到 AXI SRAM

大块缓冲（画布、LVGL 缓冲）默认落 DTCM，128KB 容易被占满。搬到 AXI SRAM 只需要两步：

1. `lcd_conf.h` 里：
   ```c
   #define LCD_BUF_ATTR  __attribute__((section(".lcd_buf"), aligned(32)))
   ```
2. 工程根目录 `CMakeLists.txt`（**CubeMX 不会覆盖这个文件**，已验证）里加：
   ```cmake
   target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--section-start=.lcd_buf=0x24000000)
   ```

本工程已经配好这两步（`.lcd_buf` 落在 0x24000000，画布 50KB 不占 DTCM）。
要注意 `RAM` 那一行在 `size` 输出里仍然显示 0%，因为这个段是通过链接选项定位的。

### 6.1 DMA 异步刷屏（`LCD_USE_DMA`）——上板 bring-up 步骤

用 MDMA 通道 0 把缓冲搬到 GRAM 数据口，CPU 同时渲染下一块。
**这段代码没在真实硬件上验证过**，请按下面顺序逐条确认：

1. **源缓冲的位置**：必须在 AXI SRAM(0x24000000) 或 D2 SRAM，不能在 DTCM。
   驱动会自己判断：如果源在 DTCM/ITCM，会自动退回同步刷屏并把错误标志置 1。
2. **先小后大**：先 `LCD_CanvasPushRows(0, 0, 8)` 推 8 行，和同步刷屏的画面比对；
   一致了再推整屏。
3. **查错误标志**：`LCD_DMA_GetError()` —— 0 正常；1 = 源在 DTCM，或 MDMA 报了
   TEIF（传送错误，通常是地址不可达）。
4. **花屏/错位**：确认 FMC 写时序够慢（`LCD_FMC_WR_DATAST` 别设太小），
   以及启动 DMA 前窗口已开好（驱动里已保证）。
5. **寄存器级排查**：`MDMA_Channel0->CISR` 的 TEIF(bit0)/CTCIF(bit1)；
   TEIF=1 表示总线错误。`MDMA_Channel0->CBNDTR` 的单位是**字节**（`像素数 × 2`）。

> 参考：ST 官方 `STM32H743I-EVAL` 的 BSP 里，SDRAM(FMC) 的 DMA 搬运用的就是
> `MDMA_Channel0`（见 `stm32h743i_eval_sdram.h` 的 `SDRAM_MDMAx_CHANNEL`）。

## 7. CubeMX 配置（已在 `H743ZGT6_DDR3_LCD.ioc` 里完成）

1. `FMC` 使能，模式 **NorPsramChipSelect4_1**（LCD Interface），引脚见第 1 节
2. NOR/PSRAM 1：Data = 16bit、Extended mode = Enabled、
   写 ADDSET=2 / ADDHLD=1 / DATAST=12，读 ADDSET=15 / ADDHLD=15 / DATAST=60
3. `I2C1` 使能（I2C 模式），`SCL=PB8`、`SDA=PB9`，`Timing = 0x10B0474D`
   （I2CCLK=PCLK1=120MHz 下的 400kHz：PRESC=1、SCLL=77、SCLH=71、SCLDEL=11、SDADEL=0）
4. GPIO：`LCD_BL(PB15)`、`LCD_RST(PB14)`、`CT_RST(PB0)` 推挽输出；`CT_INT(PC13)` 输入+上拉
5. 工程保持纯 LL；CubeMX 为 FMC 生成空的 `MX_FMC_Init()`，在其
   `USER CODE BEGIN FMC_Init 2` 里调用 `LCD_Bus_Init()`

**重要：CubeMX 生成的 `MPU_Config()` 把 `0x6000_0000~0xFFFF_FFFF` 设为 no-access，
直接访问 FMC 会进 MemManage 异常。** `lcd_bus.c` 加了 Region1（256MB、device、非缓存、
全权限）覆盖它 —— 这是 H7 上 FMC 屏不亮最常见的原因。

## 8. 移植到别的板子 / 工程

| 要改什么 | 改哪里 |
|---|---|
| 引脚、分辨率、时序、面板参数 | `BSP/Inc/lcd_conf.h`（全部集中在这里） |
| 非 CubeMX 工程 | `#define LCD_PORT_NO_CUBEMX 1`，然后自己定义第 4 节的引脚宏 |
| 换延时实现（非 LL） | 重定义 `LCD_DELAY_MS(ms)` |
| 换 FMC bank（NE1/2/3） | `LCD_FMC_BANK_BASE`（0x60000000 / 0x64000000 / 0x68000000）+ 改 `lcd_bus.c` 里的 BTCR/BWTR 索引 |
| 换 RS 地址线 | `LCD_FMC_RS_LINE`（A6 → 6；A18 → 18） |
| 不想用某个模块 | `LCD_USE_TEXT` / `LCD_USE_CANVAS` / `LCD_USE_DMA` 置 0 |

驱动分层：`lcd_conf.h`（板级）→ `lcd_bus.c`（总线，H7 专用）→ `lcd_ili9806.c`（面板）
→ `lcd.c`（绘图/通用原语）→ `Port/*`（图形库适配）。移植时只动上层即可。

## 9. 点屏排查

| 现象 | 先查什么 |
|---|---|
| 背光亮但白屏/花屏 | `LCD_ILI9806_PANEL_RES`（0xF7）是否与该屏匹配 |
| 颜色红蓝颠倒 | `LCD_ILI9806_BGR` 改成 0 |
| 图像上下/左右反了 | `LCD_MADCTL_DIRx` |
| `lcddev.id` 不是 0x9806 | 并口接线/时序：先加大 `LCD_FMC_WR_DATAST`，确认 CS/RS/WR |
| 读 ID 触发 MemManage | MPU 区域没生效：确认 `LCD_Bus_Init()` 被调用 |
| `GT911_Init()` 返回 1 | 触摸没接在 PB8/PB9／上拉电阻缺失／CT_INT 复位时序不对 |
| 触摸能读 ID 但坐标不对 | `GT911_GetResolution()` 看芯片分辨率，必要时 `CT_SWAP_XY=1` |
| I²C 一直 BUSY | 从机把 SDA 拉死：把 SCL 当 GPIO 手动发 9 个时钟再重初始化 |
| DMA 刷屏花屏/撕裂 | 源缓冲是否在 DTCM（DMA 访问不到）／传输没等完就启动下一块 |

## 10. 性能

8080 屏每画一个点都要先写 `0x2A/0x2B/0x2C` 共 11 个总线周期（约 600ns @15MHz），
比数据本身贵十几倍。所以驱动里凡是能"开一次窗 + 连续写"的都那么做：

* `LCD_BlitArea`（pitch == w）→ 整块一次开窗
* `LCD_DrawHLine` / `LCD_DrawVLine` → 一次开窗 + 连续写（比逐点快约 10 倍）
* `LCD_DrawLine` → Bresenham + 同行/同列连续段合并
* `LCD_ShowChar` → 每个字符只开一次窗

**真正的速度上限是 FMC 的 `LCD_FMC_WR_DATAST`**：

| DATAST | 周期 | 单像素 | 等效频率 | 800×480 全屏 |
|---|---|---|---|---|
| 12（默认） | 16 | 66ns | 15MHz | ≈25.6ms |
| 8 | 12 | 50ns | 20MHz | ≈19.2ms |
| 5 | 9 | 37ns | 27MHz | ≈14.2ms |

改完必须实测整屏显示是否稳定（花屏/闪烁就是太快了）。
> 注意：32 位访问、DMA 都**不会**突破这个上限（瓶颈是 FMC 编程的 DATAST），
> DMA 的价值在于把 CPU 解放出来同时渲染下一块 —— 配合 LVGL 双缓冲/画布才划算。

## 11. 验证状态

| 项目 | 状态 |
|---|---|
| CubeMX 配置（FMC / I2C1 / GPIO / MPU） | ✅ 用 CubeMX 校验通过并重新生成工程 |
| 固件编译（Debug + Release，arm-none-eabi-gcc 14.3） | ✅ 0 error / 0 warning |
| LVGL v8 / v9 适配层 | ✅ 用最小 LVGL 桩编译通过（0 warning），**未接真实 LVGL** |
| EmberGL 适配层 | ✅ **对着官方 EmberGL 源码编译通过**（0 warning），未上板跑渲染 |
| 软件 I²C 备用分支 | ✅ 编译通过 |
| DMA（MDMA）路径 | ⚠ 编译通过，**未上板验证**（见 6.1 bring-up 步骤） |
| 光栅化算法（线段分段） | ✅ **宿主机单元测试 10 万+ 用例全过**，与逐点 Bresenham 像素一致 |
| 实际点屏 / 触摸 / 性能 | ⚠ 需要硬件 |

### 宿主机单元测试怎么跑

```bash
cd tests
gcc -O2 -Wall -Wextra -I../BSP/Inc test_lcd_raster.c ../BSP/Src/lcd_raster.c -o test_raster
./test_raster
```

输出示例（穷举 17⁴ + 2 万随机 + 边界情形）：

```
结果: 用例 103533, 失败 0, 非法段 0
     共输出 630608 段 / 1334797 像素
PASS
```

`630608 段 / 1334797 像素` 说明平均每段 2.1 个像素 —— 也就是优化后开窗次数
比逐点画少了大约一半（开窗是 8080 屏最贵的操作）。
