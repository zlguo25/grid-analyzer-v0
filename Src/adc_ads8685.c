/**
  ******************************************************************************
  * @file           : adc_ads8685.c
  * @brief          : ADS8685 Daisy Chain 驱动实现
  ******************************************************************************
  */
#include "adc_ads8685.h"

/* ---------- 事件标志 ---------- */
volatile uint8_t evt_sample_done = 0;
volatile uint8_t adc_spi_timeout_error = 0;
volatile uint8_t adc_led_slow_tick = 0;

/* ---------- 采样数据缓冲区 ---------- */
uint16_t adc_voltage_buf[ADC_SAMPLE_COUNT];
uint16_t adc_current_buf[ADC_SAMPLE_COUNT];

static uint32_t sample_count = 0;

/* ---------- SPI 超时参数 ---------- */
#define SPI_TIMEOUT_MS   10U

/* ---------- 初始化 ---------- */

static void adc_write_command(uint32_t cmd)
{
    uint16_t high_word = (uint16_t)(cmd >> 16);
    uint16_t low_word  = (uint16_t)(cmd);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&high_word, 1, SPI_TIMEOUT_MS);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&low_word,  1, SPI_TIMEOUT_MS);
}

void adc_ads8685_init(void)
{
    HAL_Delay(20);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    adc_write_command(CMD_RANGE_10V24);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    adc_write_command(CMD_SDO_DEFAULT);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    sample_count = 0;
    evt_sample_done = 0;
    adc_spi_timeout_error = 0;
    adc_led_slow_tick = 0;
}

static HAL_StatusTypeDef spi_receive_timeout(uint8_t *pData)
{
    uint32_t tickstart = HAL_GetTick();
    SET_BIT(hspi3.Instance->CR1, SPI_CR1_SPE);
    *((__IO uint16_t *)&hspi3.Instance->DR) = 0xFFFF;
    while (__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_RXNE) == RESET) {
        if ((HAL_GetTick() - tickstart) >= SPI_TIMEOUT_MS) {
            __HAL_SPI_DISABLE(&hspi3);
            return HAL_TIMEOUT;
        }
    }
    *((uint16_t *)pData) = (uint16_t)hspi3.Instance->DR;
    return HAL_OK;
}

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

    if (adc_spi_timeout_error) return;

    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    status = spi_receive_timeout((uint8_t *)&temp);
    if (status != HAL_OK) goto spi_error;
    status = spi_receive_timeout((uint8_t *)&adc_current_buf[sample_count]);
    if (status != HAL_OK) goto spi_error;
    status = spi_receive_timeout((uint8_t *)&temp);
    if (status != HAL_OK) goto spi_error;
    status = spi_receive_timeout((uint8_t *)&adc_voltage_buf[sample_count]);
    if (status != HAL_OK) goto spi_error;

    sample_count++;
    if (sample_count >= ADC_SAMPLE_COUNT) {
        sample_count = 0;
        evt_sample_done = 1;
    }
    return;

spi_error:
    adc_spi_timeout_error = 1;
    evt_sample_done = 0;
    HAL_TIM_Base_Stop_IT(&htim2);
    adc_led_slow_tick = 0;   /* 启动慢闪 */
}