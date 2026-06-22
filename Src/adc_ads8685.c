/**
  ******************************************************************************
  * @file           : adc_ads8685.c
  * @brief          : ADS8685 Daisy Chain 驱动实现
  ******************************************************************************
  */
#include "adc_ads8685.h"

/* ---------- 事件标志 ---------- */
volatile uint8_t evt_sample_done = 0;

/* ---------- 采样数据缓冲区 ---------- */
uint16_t adc_voltage_buf[ADC_SAMPLE_COUNT];   /* 电压通道 (ADC1 低16b) */
uint16_t adc_current_buf[ADC_SAMPLE_COUNT];   /* 电流通道 (ADC2 低16b) */

static uint32_t sample_count = 0;

/* ---------- SPI 超时参数 ---------- */
#define SPI_TIMEOUT_MS   10U     /* 单次 SPI 接收超时 (ms) */

/* ---------- 错误状态 ---------- */
static volatile uint8_t spi_timeout_error = 0;       /* SPI 超时错误标志 */
static volatile uint8_t led_error_tick_count = 0;    /* LED 错误闪烁计数器 */

/* ---------- 初始化 ---------- */

/**
  * @brief  通过 SPI 写入 ADS8685 32-bit 命令字
  */
static void adc_write_command(uint32_t cmd)
{
    uint16_t high_word = (uint16_t)(cmd >> 16);
    uint16_t low_word  = (uint16_t)(cmd);

    HAL_SPI_Transmit(&hspi3, (uint8_t *)&high_word, 1, SPI_TIMEOUT_MS);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&low_word,  1, SPI_TIMEOUT_MS);
}

/**
  * @brief  初始化 ADS8685
  */
void adc_ads8685_init(void)
{
    HAL_Delay(20);

    /* 1. 设置输入范围 ±10.24V */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    adc_write_command(CMD_RANGE_10V24);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);

    HAL_Delay(1);

    /* 2. 确认 SDO_CTL 为默认值 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    adc_write_command(CMD_SDO_DEFAULT);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);

    /* 确保 CONVST 为低电平 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    sample_count = 0;
    evt_sample_done = 0;
    spi_timeout_error = 0;
    led_error_tick_count = 0;
}

/**
  * @brief  SPI 带超时的 16-bit 接收
  * @retval HAL_OK (0) 成功, 非0 超时
  */
static HAL_StatusTypeDef spi_receive_timeout(uint8_t *pData)
{
    uint32_t tickstart = HAL_GetTick();

    /* 使能 SPI 接收 */
    SET_BIT(hspi3.Instance->CR1, SPI_CR1_SPE);
    /* 发送 dummy 数据来产生 SCLK */
    *((__IO uint16_t *)&hspi3.Instance->DR) = 0xFFFF;

    /* 等待 RXNE */
    while (__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_RXNE) == RESET) {
        if ((HAL_GetTick() - tickstart) >= SPI_TIMEOUT_MS) {
            /* 超时：禁用 SPI 并清标志 */
            __HAL_SPI_DISABLE(&hspi3);
            return HAL_TIMEOUT;
        }
    }

    /* 读取数据 */
    *((uint16_t *)pData) = (uint16_t)hspi3.Instance->DR;
    return HAL_OK;
}

/**
  * @brief  LED1 错误闪烁 — 快速跳变发送错误码
  * @param  error_code: 错误码 (闪烁次数)
  * @note   在 TIM3 ISR 中每 100ms 调用一次本函数，每次切换 LED1 状态
  */
void led_error_blink(uint8_t error_code)
{
    /* 此函数由 led_indicator_tick 间接调用，见下方 read_sample 中的错误分支 */
    (void)error_code;
}

/**
  * @brief  单次采样 — 带超时保护的 SPI 读取
  */
void adc_ads8685_read_sample(void)
{
#if SELF_TEST
    sample_count++;
    if (sample_count >= ADC_SAMPLE_COUNT) {
        sample_count = 0;
        evt_sample_done = 1;
    }
    return;
#endif

    HAL_StatusTypeDef status;
    uint16_t temp;

    /* 如果已经触发 SPI 超时错误，直接返回（不做任何操作，等状态机自然超时） */
    if (spi_timeout_error) {
        return;
    }

    /* ① CONVST↑ → 采样 + 启动转换 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);

    /* ② 等待 t_CONV = 1µs */
    delay_us(1);

    /* ③ CONVST↓ → 结果就绪 + SDO 驱动 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    /* ④ SPI 4×16-bit 读取 (64 SCLK 边沿)，每次 10ms 超时 */

    /* ADC2 高16b → 丢弃 */
    status = spi_receive_timeout((uint8_t *)&temp);
    if (status != HAL_OK) goto spi_error;

    /* ADC2 低16b → 电流 */
    status = spi_receive_timeout((uint8_t *)&adc_current_buf[sample_count]);
    if (status != HAL_OK) goto spi_error;

    /* ADC1 高16b → 丢弃 */
    status = spi_receive_timeout((uint8_t *)&temp);
    if (status != HAL_OK) goto spi_error;

    /* ADC1 低16b → 电压 */
    status = spi_receive_timeout((uint8_t *)&adc_voltage_buf[sample_count]);
    if (status != HAL_OK) goto spi_error;

    /* ⑤ 计数 */
    sample_count++;
    if (sample_count >= ADC_SAMPLE_COUNT) {
        sample_count = 0;
        evt_sample_done = 1;
    }
    return;

spi_error:
    /* SPI 超时：立即停止 TIM2，设置错误标志 */
    spi_timeout_error = 1;
    evt_sample_done = 0;
    HAL_TIM_Base_Stop_IT(&htim2);

    /* LED1 快速闪烁（5Hz = 50ms 翻转）指示 SPI 错误 */
    led_error_tick_count = 10;    /* 闪烁 10 次（每 50ms 一次） */
}