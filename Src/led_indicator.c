/**
  ******************************************************************************
  * @file           : led_indicator.c
  * @brief          : LED 状态指示模块实现
  ******************************************************************************
  */
#include "led_indicator.h"

static led_indicator_mode_t current_mode = LED_MODE_HEARTBEAT_ONLY;

/**
  * @brief  初始化 LED 指示模块
  */
void led_indicator_init(void)
{
    current_mode = LED_MODE_HEARTBEAT_ONLY;

    /* LED 低电平点亮 */
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);   /* LED1 灭 */
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET); /* LED2 亮（常亮） */
}

/**
  * @brief  切换 LED 工作模式
  */
void led_indicator_set_mode(led_indicator_mode_t mode)
{
    current_mode = mode;

    if (mode == LED_MODE_HEARTBEAT_ONLY) {
        /* 回到 IDLE 模式：LED2 恢复常亮（低电平亮） */
        HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    }
}

/**
  * @brief  TIM3 每 100ms 调用
  */
void led_indicator_tick(void)
{
    /* LED1 — 始终翻转（系统心跳） */
    HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);

    /* LED2 — ACTIVE 模式翻转，否则常亮 */
    if (current_mode == LED_MODE_ACTIVE) {
        HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    } else {
        /* 低电平亮 → 常亮时保持低电平 */
        HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    }
}