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

/**
 * @brief 向菊花链中的两个 ADS8685 写入配置命令
 * @param cmd_near 近端芯片（靠近 MCU）收到的 32-bit 命令
 * @param cmd_far  远端芯片（远离 MCU）收到的 32-bit 命令
 *
 * 发送顺序：cmd_far 高 16 → cmd_far 低 16 → cmd_near 高 16 → cmd_near 低 16
 * 当 CONVST 上升沿锁存时，先进入链的 32 bits 到达远端芯片，后进入的到达近端芯片。
 */
static void adc_write_command(uint32_t cmd_near, uint32_t cmd_far)
{
    uint16_t words[4];
    words[0] = (uint16_t)(cmd_far >> 16);   /* 远端芯片命令高 16-bit */
    words[1] = (uint16_t)(cmd_far);         /* 远端芯片命令低 16-bit */
    words[2] = (uint16_t)(cmd_near >> 16);  /* 近端芯片命令高 16-bit */
    words[3] = (uint16_t)(cmd_near);        /* 近端芯片命令低 16-bit */
    HAL_SPI_Transmit(&hspi3, (uint8_t *)words, 4, HAL_MAX_DELAY);
}

void adc_ads8685_init(void)
{
    HAL_Delay(20);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    /* 两个芯片都配置为 ±10.24V 量程 */
    adc_write_command(CMD_RANGE_10V24, CMD_RANGE_10V24);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
    /* 两个芯片都配置 SDO_CTL = 0x00（daisy chain 默认） */
    adc_write_command(CMD_SDO_DEFAULT, CMD_SDO_DEFAULT);
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
