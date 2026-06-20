/**
  ******************************************************************************
  * @file           : led_indicator.h
  * @brief          : LED 状态指示模块 - 系统心跳 + 活动闪烁
  ******************************************************************************
  */
#ifndef __LED_INDICATOR_H
#define __LED_INDICATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum {
    LED_MODE_HEARTBEAT_ONLY = 0,  /* 仅 LED1 心跳翻转，LED2 常亮 */
    LED_MODE_ACTIVE         = 1   /* LED1 心跳翻转 + LED2 活动闪烁 */
} led_indicator_mode_t;

/**
  * @brief  初始化 LED 指示模块
  * @note   调用此函数后，LED2 设为常亮（GPIO_PIN_SET）
  */
void led_indicator_init(void);

/**
  * @brief  切换 LED 工作模式
  * @param  mode : LED_MODE_HEARTBEAT_ONLY 或 LED_MODE_ACTIVE
  * @note   由状态机在状态切换时调用
  */
void led_indicator_set_mode(led_indicator_mode_t mode);

/**
  * @brief  TIM3 中断回调中调用，每 100ms 触发一次
  * @note   由 HAL_TIM_PeriodElapsedCallback 调用
  *         - LED1 始终翻转（系统心跳）
  *         - LED2 在 ACTIVE 模式下翻转，否则保持常亮
  */
void led_indicator_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_INDICATOR_H */