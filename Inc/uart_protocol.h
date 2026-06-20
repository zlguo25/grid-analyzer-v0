/**
  ******************************************************************************
  * @file           : uart_protocol.h
  * @brief          : UART 通信协议模块 — 上位机指令解析 + 数据帧组包发送
  ******************************************************************************
  */
#ifndef __UART_PROTOCOL_H
#define __UART_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <string.h>

extern UART_HandleTypeDef hlpuart1;

/* ---------- 帧格式常量 ---------- */
#define CMD_FRAME_LEN         4U      /* 指令帧长度 */
#define CMD_HEADER            0xAA
#define CMD_START             0x01
#define CMD_STOP              0x02

#define DATA_FRAME_HEADER_H   0xBB
#define DATA_FRAME_HEADER_L   0xBB
#define DATA_FRAME_TYPE       0x0001
#define DATA_FRAME_HEADER_LEN 6U      /* 帧头2 + 类型2 + 长度2 */
#define SAMPLE_COUNT          16384

/* 上位机 → MCU: [0xAA] [CMD] [PARAM] [CHECKSUM] */

/**
  * @brief  UART 接收环形缓冲区容量
  */
#define UART_RX_BUF_SIZE      64

/* ---------- 外部可见变量 — 事件标志 ---------- */
extern volatile uint8_t evt_start_received;
extern volatile uint8_t evt_tx_done;

/* ---------- API ---------- */

/**
  * @brief  初始化 UART 协议模块
  * @note   开启 UART 接收中断，准备接收 1 字节
  */
void uart_protocol_init(void);

/**
  * @brief  UART 接收中断回调处理
  * @note   由 HAL_UART_RxCpltCallback 调用
  */
void uart_protocol_rx_callback(void);

/**
  * @brief  组装数据帧，通过 UART 发送
  * @param  voltage_buf : 电压数据缓冲区 (16384 个 uint16_t)
  * @param  current_buf : 电流数据缓冲区 (16384 个 uint16_t)
  * @note   发送完成后置位 evt_tx_done
  */
void uart_protocol_send_data(const uint16_t *voltage_buf, const uint16_t *current_buf);

/**
  * @brief  计算校验和（XOR 累加）
  */
uint8_t uart_calc_checksum(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __UART_PROTOCOL_H */