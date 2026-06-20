/**
  ******************************************************************************
  * @file           : app_state_machine.h
  * @brief          : 应用状态机 — IDLE / ADC_CONV / DATA_TRANS
  ******************************************************************************
  */
#ifndef __APP_STATE_MACHINE_H
#define __APP_STATE_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim2;

/* ---------- 状态枚举 ---------- */
typedef enum {
    STATE_IDLE       = 0,
    STATE_ADC_CONV   = 1,
    STATE_DATA_TRANS = 2
} app_state_t;

/**
  * @brief  状态机初始化
  * @note   设置初始状态 = IDLE，清除事件标志
  */
void app_state_machine_init(void);

/**
  * @brief  状态机主循环处理（在 while(1) 中轮询调用）
  * @note
  *   IDLE:        等待 evt_start_received → 启动 TIM2 → STATE_ADC_CONV
  *   ADC_CONV:    等待 evt_sample_done    → 停止 TIM2 → STATE_DATA_TRANS
  *   DATA_TRANS:  等待 evt_tx_done        → STATE_IDLE
  */
void app_state_machine_run(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_STATE_MACHINE_H */