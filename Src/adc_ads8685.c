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
#define SPI_TIMEOUT_CYCLES  170000000U / 100U   /* 10ms @ 170MHz */

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

static int spi_rx16_dwt(uint16_t *data)
{
    uint32_t start = DWT->CYCCNT;
    /* 写 DR 产生 16 SCLK */
    *((__IO uint16_t *)&hspi3.Instance->DR) = 0x0000;
    while (!(__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_RXNE))) {
        if ((DWT->CYCCNT - start) > SPI_TIMEOUT_CYCLES) return 0;
    }
    *data = (uint16_t)hspi3.Instance->DR;
    return 1;
}

void adc_ads8685_read_sample(void)
{
    uint16_t temp;
    if (adc_spi_timeout_error) return;

    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);

    if (!spi_rx16_dwt(&temp)) goto spi_error;
    if (!spi_rx16_dwt(&adc_current_buf[sample_count])) goto spi_error;
    if (!spi_rx16_dwt(&temp)) goto spi_error;
    if (!spi_rx16_dwt(&adc_voltage_buf[sample_count])) goto spi_error;

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