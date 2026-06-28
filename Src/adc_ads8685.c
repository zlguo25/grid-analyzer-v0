/**
  ******************************************************************************
  * @file           : adc_ads8685.c
  * @brief          : ADS8685 Daisy Chain 驱动实现
  ******************************************************************************
  */
#include "adc_ads8685.h"

volatile uint8_t evt_sample_done = 0;
volatile uint8_t adc_spi_timeout_error = 0;
volatile uint8_t adc_led_slow_tick = 0;

uint16_t adc_voltage_buf[ADC_SAMPLE_COUNT];
uint16_t adc_current_buf[ADC_SAMPLE_COUNT];

static uint32_t sample_count = 0;

static void adc_write_command(uint32_t cmd)
{
    uint16_t high_word = (uint16_t)(cmd >> 16);
    uint16_t low_word  = (uint16_t)(cmd);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&high_word, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&low_word,  1, HAL_MAX_DELAY);
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

void adc_ads8685_read_sample(void)
{
    uint16_t tx_buf[4] = {0, 0, 0, 0};  /* 4×16-bit = 64 SCLK */
    uint16_t rx_buf[4];
    HAL_StatusTypeDef status;

    if (adc_spi_timeout_error) return;

    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    /* 一次收发 4 个 16-bit 字，正好产生 64 个 SCLK 周期 */
    status = HAL_SPI_TransmitReceive(&hspi3, (uint8_t *)tx_buf, (uint8_t *)rx_buf, 4, HAL_MAX_DELAY);
    if (status != HAL_OK) goto spi_error;

    adc_current_buf[sample_count] = rx_buf[1];
    adc_voltage_buf[sample_count] = rx_buf[3];

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
    adc_led_slow_tick = 0;
}