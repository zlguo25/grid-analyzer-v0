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

/* ---------- 初始化 ---------- */

/**
  * @brief  通过 SPI 写入 ADS8685 32-bit 命令字
  * @param  cmd : 32-bit 命令字
  * @note   SPI3 为 16-bit data size，需分 2 次传输
  */
static void adc_write_command(uint32_t cmd)
{
    uint16_t high_word = (uint16_t)(cmd >> 16);
    uint16_t low_word  = (uint16_t)(cmd);

    HAL_SPI_Transmit(&hspi3, (uint8_t *)&high_word, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&low_word,  1, HAL_MAX_DELAY);
}

/**
  * @brief  初始化 ADS8685
  * @note   配置两个 ADC（daisy chain 下共享 CONVST/SDI，写一次即同时生效）
  */
void adc_ads8685_init(void)
{
    /* 等待内部参考电压稳定 (~20ms) */
    HAL_Delay(20);

    /* 1. 设置输入范围 ±10.24V */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);  /* CONVST↓ 开始帧 */
    adc_write_command(CMD_RANGE_10V24);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);    /* CONVST↑ 生效 */

    HAL_Delay(1);

    /* 2. 确认 SDO_CTL 为默认值 (daisy chain 模式) */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    adc_write_command(CMD_SDO_DEFAULT);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);

    /* 确保 CONVST 为低电平，准备进入采样 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    sample_count = 0;
    evt_sample_done = 0;
}

/**
  * @brief  单次采样 — 同一 ISR 中完成"采样+转换+读取"
  * @note   由 TIM2 ISR 调用 (30.5µs 周期)
  */
void adc_ads8685_read_sample(void)
{
#if SELF_TEST
    /* 自测模式：纯计数器，无SPI操作 */
    sample_count++;
    if (sample_count >= ADC_SAMPLE_COUNT) {
        sample_count = 0;
        evt_sample_done = 1;
    }
#else
    uint16_t temp;

    /* ① CONVST↑ → 采样 + 启动转换 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);

    /* ② 等待 t_CONV = 1µs */
    delay_us(1);

    /* ③ CONVST↓ → 结果就绪 + SDO 驱动 */
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    /* ④ SPI 4×16-bit 读取 (64 SCLK 边沿) */
    HAL_SPI_Receive(&hspi3, (uint8_t *)&temp, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi3, (uint8_t *)&adc_current_buf[sample_count], 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi3, (uint8_t *)&temp, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi3, (uint8_t *)&adc_voltage_buf[sample_count], 1, HAL_MAX_DELAY);

    sample_count++;
    if (sample_count >= ADC_SAMPLE_COUNT) {
        sample_count = 0;
        evt_sample_done = 1;
    }
#endif
}
