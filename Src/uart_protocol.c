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
static uint8_t  rx_buf[8];                   /* 环形接收缓冲 */
static uint8_t  rx_head = 0;
static uint8_t  rx_tail = 0;
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
    rx_head = 0;
    rx_tail = 0;
    evt_start_received = 0;
    evt_tx_done = 0;

    /* 使能 RXNE 中断，不使用 HAL_UART_Receive_IT */
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_RXNE);
}

/**
  * @brief  处理接收到的字节（从环形缓冲区取出处理）
  */
static void process_rx_byte(uint8_t data)
{
    uint8_t cs;
    static uint8_t start_blink_cnt = 0;

    /* LED 调试：每收到一个字节短闪一次 */
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    for (volatile int i = 0; i < 2000; i++);  /* 约 20ms 延时 */
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);

    cmd_buf[cmd_buf_idx++] = data;

    if (cmd_buf_idx >= CMD_FRAME_LEN) {
        /* 完整帧已收到 */
        cmd_buf_idx = 0;

        /* 校验帧头 */
        if (cmd_buf[0] != CMD_HEADER) {
            return;
        }

        /* 校验 checksum */
        cs = uart_calc_checksum(cmd_buf, 3);
        if (cs != cmd_buf[3]) {
            return;
        }

        /* 解析指令 */
        if (cmd_buf[1] == CMD_START) {
            evt_start_received = 1;
            /* LED 调试：收到 START 后快闪 3 次 */
            for (start_blink_cnt = 0; start_blink_cnt < 3; start_blink_cnt++) {
                HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
                for (volatile int i = 0; i < 5000; i++);  /* 约 50ms */
                HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
                for (volatile int i = 0; i < 5000; i++);  /* 约 50ms */
            }
        }
        /* CMD_STOP 由状态机处理 */
    }
}

/**
  * @brief  UART 接收处理（从中断调用）
  */
void uart_protocol_rx_process(void)
{
    /* 处理环形缓冲区中的所有数据 */
    while (rx_head != rx_tail) {
        uint8_t data = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & 0x07;  /* 环形缓冲 8 字节 */
        process_rx_byte(data);
    }
}

/**
  * @brief  向接收缓冲区添加数据（由中断调用）
  */
void uart_protocol_rx_byte(uint8_t data)
{
    uint8_t next_head = (rx_head + 1) & 0x07;
    if (next_head != rx_tail) {  /* 缓冲区未满 */
        rx_buf[rx_head] = data;
        rx_head = next_head;
    }
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
    
    /* 采样时长：使用宏定义计算字节值 */
    {
        uint64_t sample_time = DATA_ATTR_SAMPLE_TIME_US;  /* 500000 */
        send_buf[12] = (uint8_t)(sample_time);
        send_buf[13] = (uint8_t)(sample_time >> 8);
        send_buf[14] = (uint8_t)(sample_time >> 16);
        send_buf[15] = (uint8_t)(sample_time >> 24);
        send_buf[16] = (uint8_t)(sample_time >> 32);
        send_buf[17] = (uint8_t)(sample_time >> 40);
    }
    
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
