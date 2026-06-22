/**
  ******************************************************************************
  * @file           : adc_ads8685.h
  * @brief          : ADS8685 Daisy Chain 驱动 — 双通道同步采样
  ******************************************************************************
  */
#ifndef __ADC_ADS8685_H
#define __ADC_ADS8685_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

extern SPI_HandleTypeDef hspi3;
extern TIM_HandleTypeDef htim2;

/* ---------- 采样参数 ---------- */
#define ADC_SAMPLE_COUNT     16384                   /* 每通道采样点数 */

/* ---------- ADS8685 命令字 ---------- */
#define ADS8685_CMD_WRITE     0xD0000000U
#define ADS8685_ADDR_RANGE    0x00140000U             /* RANGE_SEL_REG = 0x14 */
#define ADS8685_ADDR_SDO_CTL  0x000C0000U             /* SDO_CTL_REG   = 0x0C */
#define ADS8685_RANGE_10V24   0x00000001U             /* ±10.24V       */
#define ADS8685_SDO_DEFAULT   0x00000000U             /* daisy chain 默认值 */
#define CMD_RANGE_10V24       (ADS8685_CMD_WRITE | ADS8685_ADDR_RANGE | ADS8685_RANGE_10V24)
#define CMD_SDO_DEFAULT       (ADS8685_CMD_WRITE | ADS8685_ADDR_SDO_CTL | ADS8685_SDO_DEFAULT)

/* ---------- 事件标志 ---------- */
extern volatile uint8_t evt_sample_done;
extern volatile uint8_t adc_spi_timeout_error;     /* SPI 超时错误标志 */
extern volatile uint8_t adc_led_error_tick_count;  /* LED 错误闪烁计数 */

/* ---------- 采样数据缓冲区 — 静态全局分配 (SRAM1) ---------- */
extern uint16_t adc_voltage_buf[ADC_SAMPLE_COUNT];
extern uint16_t adc_current_buf[ADC_SAMPLE_COUNT];

/* ---------- API ---------- */
void adc_ads8685_init(void);
void adc_ads8685_read_sample(void);

/**
  * @brief  1µs 微秒延时
  */
static inline void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

#ifdef __cplusplus
}
#endif

#endif /* __ADC_ADS8685_H */