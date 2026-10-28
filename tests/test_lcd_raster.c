/**
 ******************************************************************************
 * @file    test_lcd_raster.c
 * @brief   光栅化算法的宿主机单元测试（不需要 MCU / 屏幕）
 *
 *  验证目标：优化后的"连续段合并"算法 LCD_RasterLine() 与优化前的
 *            逐点 Bresenham 输出**完全一致**（像素集合逐个比对）。
 *
 *  编译运行（PC 上有 gcc 即可）：
 *      gcc -O2 -Wall -Wextra -I../BSP/Inc test_lcd_raster.c ../BSP/Src/lcd_raster.c -o test_raster
 *      ./test_raster
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lcd_raster.h"

#define BMP_W   72
#define BMP_H   72
#define BMP_OFF 36          /* 支持负坐标 */

static unsigned char bmp_dut[BMP_W][BMP_H];
static unsigned char bmp_ref[BMP_W][BMP_H];

static long cases_run   = 0;
static long cases_fail  = 0;
static long bad_runs    = 0;    /* 非法段（len<=0 或越界） */
static long run_count   = 0;
static long pixel_count = 0;

static void bitmap_clear(unsigned char b[BMP_W][BMP_H])
{
  (void)memset(b, 0, sizeof(unsigned char) * BMP_W * BMP_H);
}

static void plot(unsigned char b[BMP_W][BMP_H], int x, int y)
{
  int ix = x + BMP_OFF;
  int iy = y + BMP_OFF;

  if (ix < 0 || ix >= BMP_W || iy < 0 || iy >= BMP_H) return;
  b[ix][iy] = 1;
}

/* ---------- 被测试对象：分段算法输出 ---------- */
static void dut_run_cb(void *ctx, int32_t x, int32_t y, int32_t len, int vertical)
{
  int32_t i;
  (void)ctx;

  run_count++;
  if (len <= 0) { bad_runs++; return; }

  for (i = 0; i < len; i++)
  {
    plot(bmp_dut,
         (int)(x + (vertical ? 0 : i)),
         (int)(y + (vertical ? i : 0)));
    pixel_count++;
  }
}

/* ---------- 参考实现：优化前的逐点 Bresenham ---------- */
static void ref_line(int x1, int y1, int x2, int y2)
{
  int dx = x2 - x1;
  int dy = y2 - y1;
  int ux = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
  int uy = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
  int x = x1, y = y1, e;

  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;

  plot(bmp_ref, x1, y1);

  if (dx >= dy)
  {
    e = -dx / 2;
    while (x != x2)
    {
      plot(bmp_ref, x, y);
      e += dy;
      if (e >= 0) { y += uy; e -= dx; }
      x += ux;
    }
  }
  else
  {
    e = -dy / 2;
    while (y != y2)
    {
      plot(bmp_ref, x, y);
      e += dx;
      if (e >= 0) { x += ux; e -= dy; }
      y += uy;
    }
  }
  plot(bmp_ref, x2, y2);
}

static void check(int x1, int y1, int x2, int y2)
{
  int x, y, bad = 0;

  cases_run++;
  bitmap_clear(bmp_dut);
  bitmap_clear(bmp_ref);

  LCD_RasterLine(x1, y1, x2, y2, dut_run_cb, NULL);
  ref_line(x1, y1, x2, y2);

  for (x = 0; x < BMP_W; x++)
  {
    for (y = 0; y < BMP_H; y++)
    {
      if (bmp_dut[x][y] != bmp_ref[x][y]) { bad = 1; }
    }
  }

  if (bad)
  {
    cases_fail++;
    if (cases_fail <= 10)
    {
      printf("  MISMATCH: (%d,%d)-(%d,%d)\n", x1, y1, x2, y2);
    }
  }
}

static unsigned long rnd_state = 12345UL;
static int rnd_range(int lo, int hi)
{
  rnd_state = rnd_state * 1103515245UL + 12345UL;
  return lo + (int)((rnd_state >> 16) % (unsigned long)(hi - lo + 1));
}

int main(void)
{
  int x1, y1, x2, y2, i;

  printf("== LCD_RasterLine vs 逐点 Bresenham ==\n");

  /* 1. 小范围穷举：能覆盖所有斜率/方向组合 */
  for (x1 = -8; x1 <= 8; x1++)
  for (y1 = -8; y1 <= 8; y1++)
  for (x2 = -8; x2 <= 8; x2++)
  for (y2 = -8; y2 <= 8; y2++)
  {
    check(x1, y1, x2, y2);
  }
  printf("  穷举 17x17x17x17 = %ld 条\n", cases_run);

  /* 2. 随机长线 */
  for (i = 0; i < 20000; i++)
  {
    check(rnd_range(-30, 30), rnd_range(-30, 30),
          rnd_range(-30, 30), rnd_range(-30, 30));
  }
  printf("  随机 + 20000 条 → 共 %ld 条\n", cases_run);

  /* 3. 边界/特殊情形 */
  check(0, 0, 0, 0);            /* 退化成一点   */
  check(0, 0, 31, 0);           /* 水平         */
  check(0, 0, 0, 31);           /* 垂直         */
  check(31, 31, 0, 0);          /* 反向对角线   */
  check(-31, 0, 31, 0);         /* 跨原点水平   */
  check(0, -31, 0, 31);         /* 跨原点垂直   */
  check(1, 1, 2, 2);            /* 最短斜线     */
  check(0, 0, 31, 1);           /* 很平的线     */
  check(0, 0, 1, 31);           /* 很陡的线     */
  check(0, 0, 31, 30);          /* 接近 45°     */
  check(31, 0, 0, 31);          /* 另一条对角   */
  check(-31, -31, 31, 31);      /* 长对角线     */
  printf("  + 特殊情形 12 条\n");

  printf("\n结果: 用例 %ld, 失败 %ld, 非法段 %ld\n", cases_run, cases_fail, bad_runs);
  printf("     共输出 %ld 段 / %ld 像素\n", run_count, pixel_count);

  if ((cases_fail == 0) && (bad_runs == 0))
  {
    printf("PASS\n");
    return 0;
  }
  printf("FAIL\n");
  return 1;
}
