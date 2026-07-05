/**
  ******************************************************************************
  * @file           : uart_protocol.c
  * @brief          : UART 通信协议实现
  ******************************************************************************
  */
#include "uart_protocol.h"

/* ---------- 事件标志 ---------- */
volatile uint8_t evt_start_received = 0;
volatile uint8_t evt_tx_done        = 0;

/* ---------- 接收缓冲区 ---------- */
static uint8_t  rx_byte;                     /* HAL_UART_Receive_IT 接收的单字节 */
static uint8_t  cmd_buf[CMD_FRAME_LEN];      /* 指令帧拼装缓冲 */
static uint8_t  cmd_buf_idx = 0;

#define TX_CHUNK_SIZE  256U   /* 每次阻塞发送的字节数 */

/**
  * @brief  计算校验和（XOR 累加）
  */
uint8_t uart_calc_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t cs = 0;
    uint16_t i;
    for (i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}

/**
  * @brief  初始化 UART 协议
  */
void uart_protocol_init(void)
{
    cmd_buf_idx = 0;
    evt_start_received = 0;
    evt_tx_done = 0;

    /* 启动接收第 1 字节 */
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
}

/**
  * @brief  UART 接收中断回调（每收到 1 字节调用）
  */
void uart_protocol_rx_callback(void)
{
    uint8_t cs;

    cmd_buf[cmd_buf_idx++] = rx_byte;

    if (cmd_buf_idx >= CMD_FRAME_LEN) {
        /* 完整帧已收到 */
        cmd_buf_idx = 0;

        /* 校验帧头 */
        if (cmd_buf[0] != CMD_HEADER) {
            HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
            return;
        }

        /* 校验 checksum */
        cs = uart_calc_checksum(cmd_buf, 3);
        if (cs != cmd_buf[3]) {
            HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
            return;
        }

        /* 解析指令 */
        if (cmd_buf[1] == CMD_START) {
            evt_start_received = 1;
        }
        /* CMD_STOP 由状态机处理 */
    }

    /* 继续接收下一字节 */
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
}

/**
  * @brief  组装数据帧并通过 UART 阻塞发送
  * @note   分块发送以节省 SRAM（不需要 65543B 的中间缓冲）
  */
void uart_protocol_send_data(const uint16_t *voltage_buf, const uint16_t *current_buf)
{
    uint8_t  send_buf[TX_CHUNK_SIZE];
    const uint32_t data_len = (uint32_t)SAMPLE_COUNT * 2U * 2U;  /* 65536 */
    uint16_t chunk_bytes;
    uint8_t  checksum = 0;
    uint32_t i;

    /* ---- 扩展帧头 (32 bytes) ---- */
    /* 基础区：帧头(2) + 类型(2) + 长度(4) = 8B */
    send_buf[0] = DATA_FRAME_HEADER_H;
    send_buf[1] = DATA_FRAME_HEADER_L;
    send_buf[2] = (uint8_t)(DATA_FRAME_TYPE);
    send_buf[3] = (uint8_t)(DATA_FRAME_TYPE >> 8);
    send_buf[4] = (uint8_t)(data_len);
    send_buf[5] = (uint8_t)(data_len >> 8);
    send_buf[6] = (uint8_t)(data_len >> 16);
    send_buf[7] = (uint8_t)(data_len >> 24);
    
    /* 属性区：采样率(4) + 采样时长(6) + 通道数(1) = 11B */
    send_buf[8]  = (uint8_t)(DATA_ATTR_SAMPLE_RATE);
    send_buf[9]  = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 8);
    send_buf[10] = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 16);
    send_buf[11] = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 24);
    
    /* 采样时长 500000 = 0x7A120 (小端序: 20 A1 07 00 00 00) */
    send_buf[12] = 0x20;  /* 500000 & 0xFF */
    send_buf[13] = 0xA1;  /* (500000 >> 8) & 0xFF */
    send_buf[14] = 0x07;  /* (500000 >> 16) & 0xFF */
    send_buf[15] = 0x00;  /* (500000 >> 24) & 0xFF */
    send_buf[16] = 0x00;  /* (500000 >> 32) & 0xFF */
    send_buf[17] = 0x00;  /* (500000 >> 40) & 0xFF */
    
    send_buf[18] = DATA_ATTR_CHANNEL_NUM;
    
    /* 保留区：13B (bytes 19-31) */
    memset(&send_buf[19], 0, 13);
    
    HAL_UART_Transmit(&hlpuart1, send_buf, DATA_FRAME_HEADER_LEN, 60000U);
    checksum = uart_calc_checksum(send_buf, DATA_FRAME_HEADER_LEN);

    /* ---- 电压数据 (32768 bytes) ---- */
    for (i = 0; i < SAMPLE_COUNT; i++) {
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U]     = (uint8_t)(voltage_buf[i]);
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U + 1U] = (uint8_t)(voltage_buf[i] >> 8);

        if ((i + 1U) % (TX_CHUNK_SIZE / 2U) == 0 || i == SAMPLE_COUNT - 1U) {
            chunk_bytes = ((i % (TX_CHUNK_SIZE / 2U)) + 1U) * 2U;
            HAL_UART_Transmit(&hlpuart1, send_buf, chunk_bytes, HAL_MAX_DELAY);
            checksum ^= uart_calc_checksum(send_buf, chunk_bytes);
        }
    }

    /* ---- 电流数据 (32768 bytes) ---- */
    for (i = 0; i < SAMPLE_COUNT; i++) {
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U]     = (uint8_t)(current_buf[i]);
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U + 1U] = (uint8_t)(current_buf[i] >> 8);

        if ((i + 1U) % (TX_CHUNK_SIZE / 2U) == 0 || i == SAMPLE_COUNT - 1U) {
            chunk_bytes = ((i % (TX_CHUNK_SIZE / 2U)) + 1U) * 2U;
            HAL_UART_Transmit(&hlpuart1, send_buf, chunk_bytes, HAL_MAX_DELAY);
            checksum ^= uart_calc_checksum(send_buf, chunk_bytes);
        }
    }

    /* ---- checksum (1 byte) ---- */
    HAL_UART_Transmit(&hlpuart1, &checksum, 1U, HAL_MAX_DELAY);

    /* 发送完成 */
    evt_tx_done = 1;
}
