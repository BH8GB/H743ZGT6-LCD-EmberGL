# H743ZGT6 LCD / EmberGL 工程

STM32H743ZGT6 驱动正点原子 4.3 寸 800×480 电容屏（NT35510 + GT911），
跑 EmberGL 3D 动画。全部代码已上板验证。

## 硬件

| 项 | 说明 |
|---|---|
| MCU | STM32H743ZGT6，LQFP144，480 MHz |
| 屏 | 正点原子 4.3" 800×480（MCU 屏），控制器 **NT35510** |
| 接口 | 16 位 8080 并口，挂 FMC Bank1 子 bank4（NE4） |
| 触摸 | GT911 电容触摸，硬件 I2C1 @ 100 kHz |
| 调试 | SWD（PA13/PA14）+ CMSIS-DAP |

### 引脚

```
数据 D0~D15   PD14 PD15 PD0 PD1 PE7~PE15 PD8 PD9 PD10
CS  (NE4)     PG12
RS  (A6)      PF12
WR  (NWE)     PD5
RD  (NOE)     PD4
LCD_RST       PB14      背光 BL_CTR   PB15
CT_SCL        PB8       CT_SDA        PB9
CT_RST        PB0       CT_INT        PC13
```

命令地址 `0x6C00007E`，数据地址 `0x6C000080`（16 位总线，A6 → HADDR[7]）。

## 目录

```
BSP/Inc, BSP/Src     显示 / 触摸 / 自检 / 时序驱动
BSP/Port             EmberGL、LVGL 适配层
third_party/EmberGL  EmberGL 源码与 p3g 资源（自包含）
Core                 CubeMX 生成的 HAL 初始化与 FreeRTOS 任务
tests                PC 端光栅化单元测试
```

关键文件：

| 文件 | 作用 |
|---|---|
| `BSP/Inc/lcd_conf.h` | **唯一需要改的配置**：引脚、几何、时序、面板、编译开关 |
| `BSP/Src/lcd_ili9806.c` | 控制器初始化表（NT35510 / ILI9806G 二选一） |
| `BSP/Src/lcd_bus.c` | FMC 时序覆盖 + MPU 配置 |
| `BSP/Src/gt911.c` | GT911 触摸驱动 |
| `BSP/Src/lcd_selftest.c` | 自检、色条、位测试、触摸轨迹 |
| `BSP/Port/lcd_port_embergl.cpp` | EmberGL tile → 屏幕的适配与条带双缓冲 |
| `BSP/Port/lcd_port_embergl_demo.cpp` | 旋转圆环示例 |

## 构建

```powershell
$env:PATH = "C:\Software\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin;" + $env:PATH
cd C:\Users\EricE\Documents\STM32_HAL\H743ZGT6_DDR3_LCD
cmake --preset Debug
cmake --build --preset Debug
```

产物在 `build/Debug/`：`.elf` / `.hex` / `.bin`。

## 烧录

这块 CMSIS-DAP 没接 nRESET，**必须**加 `--connect attach`，否则 pyocd 会在
`safe_reset_and_halt()` 断言失败：

```powershell
uv --directory C:\Users\EricE\pyocd-debug-mcp run pyocd flash `
  --erase chip --connect attach -t stm32h743xx -f 1M --uid 18912457 `
  build\Debug\H743ZGT6_DDR3_LCD.bin
```

## 运行流程

`main()` 只做 HAL/时钟/外设初始化，随后 `osKernelStart()`。显示与触摸都在
`StartDefaultTask` 里：

```
LCD_Init()            → 面板初始化（延时自动选 osDelay / HAL_Delay）
LCD_SelfTest()        → 色条 + 渐变 + FPS
osDelay(5000)
LCD_EGL_DemoInit()    → EmberGL 光栅化器初始化
for(;;) LCD_EGL_DemoRun(16)   → 旋转圆环，约 30 fps
```

## EmberGL 开关

`CMakeLists.txt` 第 29 行：

```cmake
set(LCD_ENABLE_EMBERGL 1 CACHE BOOL "Build EmberGL port and demo")
```

设为 `0` 后重新 `cmake --preset Debug`，会：

1. 不编译 EmberGL 的 6 个 `.cpp` 与适配层
2. `main.c` 里 `#if LCD_PORT_EMBERGL` 整段裁掉
3. 回落到触摸测试（`LCD_TouchInit` + `LCD_TouchLoop`）

也可以命令行覆盖，不改文件：`cmake --preset Debug -DLCD_ENABLE_EMBERGL=0`。

## 性能

整屏填充（800×480，768 KB/帧）实测：

| `LCD_FMC_WR_DATAST` | 写周期 | FPS |
|---|---|---|
| 12 | 66 ns | 42 |
| 8 | 50 ns | 57 |
| 6 | 41 ns | 69 |
| **5** | **37 ns** | **78** |

78 fps ≈ 60 MB/s，已是 16 位 / 15 MHz 并口的理论极限。再往上只能换接口
（RGB/LTDC 或 MIPI）。

## 内存布局

| 区域 | 用途 | 大小 |
|---|---|---|
| Flash | 代码 | 210 KB / 512 KB（41%） |
| DTCM | FreeRTOS 堆 24 KB、任务栈 16 KB、EmberGL 光栅化内存 39 KB | 74 KB / 128 KB |
| AXI SRAM `0x24000000` | 画布缓冲 | 50 KB |
| AXI SRAM `0x2400C800` | EmberGL 条带双缓冲 | 375 KB |

AXI SRAM 只有 512 KB，整屏 framebuffer 需要 750 KB 放不下，所以 EmberGL 采用
**半屏条带双缓冲**：渲染到离屏缓冲，整条带完成后再一次推屏，避免看到 tile 逐块绘制。

## 已知问题

1. **LCD ID 读回 0**：写通路正常、显示正常，读通路不通。不影响使用，待查 RD 接线。
2. **CubeMX 重新生成会覆盖 3 处手改**：
   - QUADSPI 六脚速度（需 `GPIO_SPEED_FREQ_VERY_HIGH`）
   - `MX_SDMMC1_SD_Init` 的“无卡不致命”
   - `SysTick_Handler` 的调度器状态保护
3. **Flash 里的 `CMakeLists.txt.bak*`**：历史备份，可删。

## 排查工具

`BSP/Src/lcd_selftest.c` 里有三个可直接调用的诊断函数：

| 函数 | 用途 |
|---|---|
| `LCD_BitTest()` | 16 根单比特色条，定位数据线断/错位 |
| `LCD_GeoTest()` | 边框 + 中心十字 + 角块，验证开窗与坐标 |
| `LCD_FontTest()` | 两行已知字符串，验证字模 |

## 单元测试

光栅化算法与平台无关，可在 PC 上跑：

```powershell
cd tests
gcc -O2 -Wall -Wextra -I../BSP/Inc test_lcd_raster.c ../BSP/Src/lcd_raster.c -o test_raster.exe
.\test_raster.exe
```

## 致谢

EmberGL 由 Jarkko Lempiainen 开发（MIT），见 `third_party/EmberGL/`。
NT35510 初始化表来自正点原子例程。
