/* GT911 电容触摸。传输层默认用硬件 I2C1（由 CubeMX 的 MX_I2C1_Init 初始化）；
 * 触摸不在硬件 I2C 引脚上时，把 CT_USE_SOFT_I2C 打开走 GPIO 模拟。
 * 复位脚 CT_RST，中断/地址选择脚 CT_INT。 */
#include "gt911.h"

#define GT911_REG_CFGVER      0x8047U
#define GT911_REG_XRES        0x8048U
#define GT911_REG_PRODUCT_ID  0x8140U
#define GT911_REG_STATUS      0x814EU
#define GT911_REG_POINTS      0x814FU

static uint8_t    gt911_addr;
static char       gt911_product_id[4];
static uint8_t    gt911_cfg_ver;
static uint16_t   gt911_res_x, gt911_res_y;
static ct_point_t gt911_points[CT_MAX_TOUCH];
static uint8_t    gt911_point_num;

#define CT_RST_H()      LL_GPIO_SetOutputPin(CT_RST_GPIO_Port, CT_RST_Pin)
#define CT_RST_L()      LL_GPIO_ResetOutputPin(CT_RST_GPIO_Port, CT_RST_Pin)
#define CT_INT_H()      LL_GPIO_SetOutputPin(CT_INT_GPIO_Port, CT_INT_Pin)
#define CT_INT_L()      LL_GPIO_ResetOutputPin(CT_INT_GPIO_Port, CT_INT_Pin)
#define CT_INT_READ()   LL_GPIO_IsInputPinSet(CT_INT_GPIO_Port, CT_INT_Pin)

/*==============================================================================
 * 传输层 ①：硬件 I2C1（返回 0 = 成功）
 *============================================================================*/
#if (CT_USE_SOFT_I2C == 0)

#define CT_I2C   I2C1

static uint8_t ct_i2c_abort(void)
{
  if (LL_I2C_IsActiveFlag_NACK(CT_I2C))
  {
    LL_I2C_ClearFlag_NACK(CT_I2C);
    LL_I2C_GenerateStopCondition(CT_I2C);
  }
  if (LL_I2C_IsActiveFlag_STOP(CT_I2C))
  {
    LL_I2C_ClearFlag_STOP(CT_I2C);
  }
  return 1U;
}

static uint8_t ct_i2c_wait_idle(void)
{
  uint32_t timeout = CT_I2C_TIMEOUT_LOOPS;

  while (LL_I2C_IsActiveFlag_BUSY(CT_I2C))
  {
    if (--timeout == 0U) return 1U;
  }
  return 0U;
}

static uint8_t ct_i2c_write(uint8_t dev_addr, const uint8_t *buf, uint16_t len)
{
  uint16_t i;
  uint32_t timeout;

  if (ct_i2c_wait_idle() != 0U) return 1U;

  LL_I2C_HandleTransfer(CT_I2C, (uint32_t)(dev_addr << 1), LL_I2C_ADDRSLAVE_7BIT,
                        len, LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_START_WRITE);

  for (i = 0; i < len; i++)
  {
    timeout = CT_I2C_TIMEOUT_LOOPS;
    while (LL_I2C_IsActiveFlag_TXIS(CT_I2C) == 0U)
    {
      if (LL_I2C_IsActiveFlag_NACK(CT_I2C)) return ct_i2c_abort();
      if (--timeout == 0U)                  return ct_i2c_abort();
    }
    LL_I2C_TransmitData8(CT_I2C, buf[i]);
  }

  timeout = CT_I2C_TIMEOUT_LOOPS;                 /* AUTOEND 会自动产生 STOP */
  while (LL_I2C_IsActiveFlag_STOP(CT_I2C) == 0U)
  {
    if (LL_I2C_IsActiveFlag_NACK(CT_I2C)) return ct_i2c_abort();
    if (--timeout == 0U)                  return ct_i2c_abort();
  }
  LL_I2C_ClearFlag_STOP(CT_I2C);
  return 0U;
}

/* 先写 16 位寄存器地址（不结束），再重复起始读 */
static uint8_t ct_i2c_read(uint8_t dev_addr, uint16_t reg, uint8_t *buf, uint16_t len)
{
  uint8_t  addr_buf[2];
  uint16_t i;
  uint32_t timeout;

  if (ct_i2c_wait_idle() != 0U) return 1U;

  addr_buf[0] = (uint8_t)(reg >> 8);
  addr_buf[1] = (uint8_t)(reg & 0xFFU);

  LL_I2C_HandleTransfer(CT_I2C, (uint32_t)(dev_addr << 1), LL_I2C_ADDRSLAVE_7BIT,
                        2U, LL_I2C_MODE_SOFTEND, LL_I2C_GENERATE_START_WRITE);

  for (i = 0; i < 2U; i++)
  {
    timeout = CT_I2C_TIMEOUT_LOOPS;
    while (LL_I2C_IsActiveFlag_TXIS(CT_I2C) == 0U)
    {
      if (LL_I2C_IsActiveFlag_NACK(CT_I2C)) return ct_i2c_abort();
      if (--timeout == 0U)                  return ct_i2c_abort();
    }
    LL_I2C_TransmitData8(CT_I2C, addr_buf[i]);
  }

  timeout = CT_I2C_TIMEOUT_LOOPS;                 /* SOFTEND 下等 TC 才能发重复起始 */
  while (LL_I2C_IsActiveFlag_TC(CT_I2C) == 0U)
  {
    if (LL_I2C_IsActiveFlag_NACK(CT_I2C)) return ct_i2c_abort();
    if (--timeout == 0U)                  return ct_i2c_abort();
  }

  LL_I2C_HandleTransfer(CT_I2C, (uint32_t)((dev_addr << 1) | 0x01U), LL_I2C_ADDRSLAVE_7BIT,
                        len, LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_START_READ);

  for (i = 0; i < len; i++)
  {
    timeout = CT_I2C_TIMEOUT_LOOPS;
    while (LL_I2C_IsActiveFlag_RXNE(CT_I2C) == 0U)
    {
      if (LL_I2C_IsActiveFlag_NACK(CT_I2C)) return ct_i2c_abort();
      if (--timeout == 0U)                  return ct_i2c_abort();
    }
    buf[i] = LL_I2C_ReceiveData8(CT_I2C);
  }

  timeout = CT_I2C_TIMEOUT_LOOPS;
  while (LL_I2C_IsActiveFlag_STOP(CT_I2C) == 0U)
  {
    if (--timeout == 0U) return ct_i2c_abort();
  }
  LL_I2C_ClearFlag_STOP(CT_I2C);
  return 0U;
}

/*==============================================================================
 * 传输层 ②：GPIO 模拟 I2C（备用）
 *============================================================================*/
#else

#define CT_SCL_H()      LL_GPIO_SetOutputPin(CT_SOFT_SCL_PORT, CT_SOFT_SCL_PIN)
#define CT_SCL_L()      LL_GPIO_ResetOutputPin(CT_SOFT_SCL_PORT, CT_SOFT_SCL_PIN)
#define CT_SDA_H()      LL_GPIO_SetOutputPin(CT_SOFT_SDA_PORT, CT_SOFT_SDA_PIN)
#define CT_SDA_L()      LL_GPIO_ResetOutputPin(CT_SOFT_SDA_PORT, CT_SOFT_SDA_PIN)
#define CT_SDA_READ()   LL_GPIO_IsInputPinSet(CT_SOFT_SDA_PORT, CT_SOFT_SDA_PIN)

static void gt911_delay_us(uint32_t us)
{
  uint32_t start  = DWT->CYCCNT;
  uint32_t cycles = us * (SystemCoreClock / 1000000U);

  while ((DWT->CYCCNT - start) < cycles) {}
}

static void ct_i2c_init_soft(void)
{
  LL_GPIO_InitTypeDef gpio = {0};

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  gpio.Pin        = CT_SOFT_SCL_PIN | CT_SOFT_SDA_PIN;
  gpio.Mode       = LL_GPIO_MODE_OUTPUT;
  gpio.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  gpio.Pull       = LL_GPIO_PULL_UP;
  LL_GPIO_Init(CT_SOFT_SCL_PORT, &gpio);

  CT_SCL_H();
  CT_SDA_H();
}

static void ct_start(void)
{
  CT_SDA_H(); CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
  CT_SDA_L();             gt911_delay_us(CT_I2C_DELAY_US);
  CT_SCL_L();             gt911_delay_us(CT_I2C_DELAY_US);
}

static void ct_stop(void)
{
  CT_SDA_L(); CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
  CT_SDA_H();             gt911_delay_us(CT_I2C_DELAY_US);
}

static uint8_t ct_send_byte(uint8_t dat)
{
  uint8_t i, ack;

  for (i = 0; i < 8U; i++)
  {
    if (dat & 0x80U) CT_SDA_H(); else CT_SDA_L();
    dat = (uint8_t)(dat << 1);
    gt911_delay_us(CT_I2C_DELAY_US);
    CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
    CT_SCL_L(); gt911_delay_us(CT_I2C_DELAY_US);
  }

  CT_SDA_H(); gt911_delay_us(CT_I2C_DELAY_US);
  CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
  ack = CT_SDA_READ() ? 1U : 0U;
  CT_SCL_L(); gt911_delay_us(CT_I2C_DELAY_US);
  return ack;
}

static uint8_t ct_read_byte(uint8_t ack)
{
  uint8_t i, dat = 0U;

  CT_SDA_H();
  for (i = 0; i < 8U; i++)
  {
    dat = (uint8_t)(dat << 1);
    gt911_delay_us(CT_I2C_DELAY_US);
    CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
    if (CT_SDA_READ()) dat |= 1U;
    CT_SCL_L(); gt911_delay_us(CT_I2C_DELAY_US);
  }

  if (ack) CT_SDA_L(); else CT_SDA_H();
  gt911_delay_us(CT_I2C_DELAY_US);
  CT_SCL_H(); gt911_delay_us(CT_I2C_DELAY_US);
  CT_SCL_L(); gt911_delay_us(CT_I2C_DELAY_US);
  CT_SDA_H();
  return dat;
}

/* 与硬件版接口一致：buf = [reg_hi, reg_lo, data...] */
static uint8_t ct_i2c_write(uint8_t dev_addr, const uint8_t *buf, uint16_t len)
{
  uint16_t i;
  uint8_t  err = 0U;

  ct_start();
  err |= ct_send_byte((uint8_t)(dev_addr << 1));
  for (i = 0; i < len; i++)
  {
    err |= ct_send_byte(buf[i]);
  }
  ct_stop();
  return err;
}

static uint8_t ct_i2c_read(uint8_t dev_addr, uint16_t reg, uint8_t *buf, uint16_t len)
{
  uint16_t i;
  uint8_t  err = 0U;

  ct_start();
  err |= ct_send_byte((uint8_t)(dev_addr << 1));
  err |= ct_send_byte((uint8_t)(reg >> 8));
  err |= ct_send_byte((uint8_t)(reg & 0xFFU));

  ct_start();
  err |= ct_send_byte((uint8_t)((dev_addr << 1) | 0x01U));
  for (i = 0; i < len; i++)
  {
    buf[i] = ct_read_byte((i == (uint16_t)(len - 1U)) ? 0U : 1U);
  }
  ct_stop();
  return err;
}

#endif /* CT_USE_SOFT_I2C */

/*==============================================================================
 * 寄存器读写 / 复位
 *============================================================================*/
static uint8_t ct_write_reg(uint16_t reg, const uint8_t *buf, uint16_t len)
{
  uint8_t tmp[2 + 8];
  uint16_t i;

  if (len > 8U) return 1U;

  tmp[0] = (uint8_t)(reg >> 8);
  tmp[1] = (uint8_t)(reg & 0xFFU);
  for (i = 0; i < len; i++)
  {
    tmp[2 + i] = buf[i];
  }
  return ct_i2c_write(gt911_addr, tmp, (uint16_t)(2U + len));
}

static uint8_t ct_read_reg(uint16_t reg, uint8_t *buf, uint16_t len)
{
  return ct_i2c_read(gt911_addr, reg, buf, len);
}

/* 复位期间 CT_INT 的电平决定 IIC 地址；低=0x5D，高=0x14 */
static void gt911_reset(uint8_t int_level)
{
  LL_GPIO_InitTypeDef gpio = {0};

  gpio.Pin        = CT_INT_Pin;
  gpio.Mode       = LL_GPIO_MODE_OUTPUT;
  gpio.Speed      = LL_GPIO_SPEED_FREQ_LOW;
  gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  gpio.Pull       = LL_GPIO_PULL_NO;
  LL_GPIO_Init(CT_INT_GPIO_Port, &gpio);

  CT_RST_L();
  if (int_level) CT_INT_H(); else CT_INT_L();
  LCD_DELAY_MS(12);                           /* RST 低 >10ms */
  CT_RST_H();
  LCD_DELAY_MS(55);                           /* INT 电平保持 >50ms 锁存地址 */

  gpio.Mode = LL_GPIO_MODE_INPUT;
  gpio.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(CT_INT_GPIO_Port, &gpio);
  LCD_DELAY_MS(10);
}

/*==============================================================================
 * 对外接口
 *============================================================================*/
uint8_t GT911_Init(void)
{
  uint8_t tmp[4];
  uint8_t i;

#if CT_USE_SOFT_I2C
  ct_i2c_init_soft();
#endif

  /* 先按 0x5D 复位，读不到产品 ID 再试 0x14 */
  for (i = 0; i < 2U; i++)
  {
    gt911_addr = (i == 0U) ? CT_GT911_ADDR : CT_GT911_ADDR_ALT;
    gt911_reset((uint8_t)((i == 0U) ? 0U : 1U));

    if (ct_read_reg(GT911_REG_PRODUCT_ID, tmp, 4U) == 0U)
    {
      if ((tmp[0] == '9') && (tmp[1] == '1'))
      {
        gt911_product_id[0] = (char)tmp[0];
        gt911_product_id[1] = (char)tmp[1];
        gt911_product_id[2] = (char)tmp[2];
        gt911_product_id[3] = (char)tmp[3];

        (void)ct_read_reg(GT911_REG_CFGVER, &gt911_cfg_ver, 1U);

        (void)ct_read_reg(GT911_REG_XRES, tmp, 4U);
        gt911_res_x = (uint16_t)((uint16_t)tmp[0] | ((uint16_t)tmp[1] << 8));
        gt911_res_y = (uint16_t)((uint16_t)tmp[2] | ((uint16_t)tmp[3] << 8));

        tmp[0] = 0U;
        (void)ct_write_reg(GT911_REG_STATUS, tmp, 1U);
        return 0U;
      }
    }
  }
  return 1U;
}

/* 没有新数据时保持上次状态：触摸芯片 75Hz 上报，调用者可能轮询更快 */
uint8_t GT911_Scan(void)
{
  uint8_t buf[1U + CT_MAX_TOUCH * 8U];
  uint8_t i;

  if (ct_read_reg(GT911_REG_STATUS, buf, 1U) != 0U) return gt911_point_num;
  if ((buf[0] & 0x80U) == 0U)                      return gt911_point_num;

  gt911_point_num = (uint8_t)(buf[0] & 0x0FU);
  if (gt911_point_num > CT_MAX_TOUCH) gt911_point_num = CT_MAX_TOUCH;

  if (gt911_point_num > 0U)
  {
    uint8_t n = (uint8_t)(1U + gt911_point_num * 8U);

    if (ct_read_reg(GT911_REG_POINTS, buf, n) == 0U)
    {
      for (i = 0U; i < gt911_point_num; i++)
      {
        /* 0x814F 起的记录本身就是 [id, xl, xh, yl, yh, sizel, sizeh, rsvd]，
           buf[0] 已是 id，不能再 +1，否则整体错位一个字节。 */
        const uint8_t *p = &buf[(uint16_t)i * 8U];

        gt911_points[i].id   = p[0];
        gt911_points[i].x    = (uint16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
        gt911_points[i].y    = (uint16_t)((uint16_t)p[3] | ((uint16_t)p[4] << 8));
        gt911_points[i].size = (uint16_t)((uint16_t)p[5] | ((uint16_t)p[6] << 8));

#if CT_SWAP_XY
        {
          uint16_t t = gt911_points[i].x;
          gt911_points[i].x = gt911_points[i].y;
          gt911_points[i].y = t;
        }
#endif
      }
    }
    else
    {
      gt911_point_num = 0U;
    }
  }

  buf[0] = 0U;
  (void)ct_write_reg(GT911_REG_STATUS, buf, 1U);
  return gt911_point_num;
}

uint8_t GT911_IsTouched(uint16_t *x, uint16_t *y)
{
  (void)GT911_Scan();

  if (gt911_point_num == 0U) return 0U;

  if (x != 0) *x = gt911_points[0].x;
  if (y != 0) *y = gt911_points[0].y;
  return 1U;
}

const ct_point_t *GT911_GetPoints(void)     { return gt911_points; }
const char       *GT911_GetProductID(void)  { return gt911_product_id; }
uint8_t           GT911_GetConfigVersion(void) { return gt911_cfg_ver; }

void GT911_GetResolution(uint16_t *x, uint16_t *y)
{
  if (x != 0) *x = gt911_res_x;
  if (y != 0) *y = gt911_res_y;
}
