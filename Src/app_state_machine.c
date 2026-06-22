/**
  ******************************************************************************
  * @file           : app_state_machine.c
  * @brief          : 应用状态机实现 (自测版：ADC→直接IDLE)
  ******************************************************************************
  */
#include "app_state_machine.h"
#include "adc_ads8685.h"
#include "uart_protocol.h"
#include "led_indicator.h"

static app_state_t current_state = STATE_IDLE;

void app_state_machine_init(void)
{
    current_state      = STATE_IDLE;
    evt_start_received  = 0;
    evt_sample_done     = 0;
    evt_tx_done         = 0;
}

void app_state_machine_run(void)
{
    switch (current_state) {

    case STATE_IDLE:
        if (evt_start_received) {
            evt_start_received = 0;
            led_indicator_set_mode(LED_MODE_ACTIVE);

            HAL_GPIO_WritePin(CONVST_GPIO_Port, CONVST_Pin, GPIO_PIN_RESET);
            evt_sample_done = 0;
            HAL_TIM_Base_Start_IT(&htim2);

            current_state = STATE_ADC_CONV;
        }
        break;

    case STATE_ADC_CONV:
        if (evt_sample_done) {
            evt_sample_done = 0;
            HAL_TIM_Base_Stop_IT(&htim2);

            /* 直接回 IDLE — 不发送 UART 数据，不等待 evt_tx_done */
            led_indicator_set_mode(LED_MODE_HEARTBEAT_ONLY);
            current_state = STATE_IDLE;
        }
        break;

    default:
        current_state = STATE_IDLE;
        break;
    }
}