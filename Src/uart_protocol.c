/**
  ******************************************************************************
  * @file           : uart_protocol.c
  * @brief          : UART 通信协议实现 - 使用IDLE中断
  ******************************************************************************
  */
#include "uart_protocol.h"
#include "adc_ads8685.h"

/* ---------- 事件标志 ---------- */
volatile uint8_t evt_start_received = 0;
volatile uint8_t evt_tx_done        = 0;

/* ---------- 接收缓冲区 ---------- */
static uint8_t  rx_buf[16];                  /* UART接收缓冲 */
static volatile uint8_t  rx_len = 0;         /* 接收到的数据长度（volatile用于中断同步） */
volatile uint8_t  rx_ready = 0;              /* 数据就绪标志（非static，供中断使用） */

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
  * @brief  处理接收到的指令帧
  */
static void process_cmd_frame(const uint8_t *data, uint8_t len)
{
    uint8_t cs;
    static uint8_t start_blink_cnt = 0;
    uint8_t i;

    /* 需要至少4字节才是一个完整帧 */
    if (len < CMD_FRAME_LEN) {
        return;
    }

    /* 从接收到的数据中找到帧头 0xAA */
    for (i = 0; i <= len - CMD_FRAME_LEN; i++) {
        if (data[i] != CMD_HEADER) {
            continue;
        }

        /* 找到帧头，检查校验和 */
        cs = uart_calc_checksum(&data[i], 3);
        if (cs != data[i + 3]) {
            continue;  /* 校验失败，继续找下一个 */
        }

        /* 校验通过，解析指令 */
        if (data[i + 1] == CMD_START) {
            evt_start_received = 1;
            /* LED 调试：收到 START 后快闪 3 次 */
            for (start_blink_cnt = 0; start_blink_cnt < 3; start_blink_cnt++) {
                HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
                for (volatile int j = 0; j < 5000; j++);  /* 约 50ms */
                HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
                for (volatile int j = 0; j < 5000; j++);  /* 约 50ms */
            }
        }
        /* CMD_STOP 由状态机处理 */
        return;  /* 处理完成 */
    }
}

/**
  * @brief  初始化 UART 协议
  */
void uart_protocol_init(void)
{
    rx_len = 0;
    evt_start_received = 0;
    evt_tx_done = 0;

    /* 使能 IDLE 中断和 RXNE 中断 */
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_IDLE);
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_RXNE);
}

/* 导出 rx_ready 供中断使用 */
volatile uint8_t* uart_protocol_get_rx_ready_flag(void)
{
    return &rx_ready;
}

/**
  * @brief  UART 接收处理 - 从主循环调用
  */
void uart_protocol_rx_process(void)
{
    if (rx_ready && rx_len >= CMD_FRAME_LEN) {
        /* 处理接收到的数据 */
        process_cmd_frame(rx_buf, rx_len);
        rx_len = 0;      /* 清空缓冲区 */
        rx_ready = 0;    /* 清除标志 */
    }
}

/**
  * @brief  向接收缓冲区添加数据（由中断调用）
  * @return 0=成功, 1=缓冲区满
  */
uint8_t uart_protocol_rx_byte(uint8_t data)
{
    if (rx_len < sizeof(rx_buf)) {
        rx_buf[rx_len++] = data;
        return 0;
    }
    return 1;  /* 缓冲区满 */
}

/**
  * @brief  获取当前接收长度（用于IDLE中断处理）
  */
uint8_t* uart_protocol_get_rx_buf(void)
{
    return rx_buf;
}

/**
  * @brief  设置接收长度（用于IDLE中断处理）
  */
void uart_protocol_set_rx_len(uint8_t len)
{
    rx_len = len;
}

/**
  * @brief  清除接收缓冲区
  */
void uart_protocol_clear_rx(void)
{
    rx_len = 0;
}

/**
  * @brief  组装数据帧并通过 UART 阻塞发送
  * @note   分块发送以节省 SRAM
  */
void uart_protocol_send_data(const uint16_t *voltage_buf, const uint16_t *current_buf)
{
    uint8_t  send_buf[TX_CHUNK_SIZE];
    const uint32_t data_len = (uint32_t)SAMPLE_COUNT * 2U * 2U;  /* 65536 */
    uint16_t chunk_bytes;
    uint8_t  checksum = 0;
    uint32_t i;

    /* ---- 扩展帧头 (32 bytes) ---- */
    send_buf[0] = DATA_FRAME_HEADER_H;
    send_buf[1] = DATA_FRAME_HEADER_L;
    send_buf[2] = (uint8_t)(DATA_FRAME_TYPE);
    send_buf[3] = (uint8_t)(DATA_FRAME_TYPE >> 8);
    send_buf[4] = (uint8_t)(data_len);
    send_buf[5] = (uint8_t)(data_len >> 8);
    send_buf[6] = (uint8_t)(data_len >> 16);
    send_buf[7] = (uint8_t)(data_len >> 24);
    
    send_buf[8]  = (uint8_t)(DATA_ATTR_SAMPLE_RATE);
    send_buf[9]  = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 8);
    send_buf[10] = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 16);
    send_buf[11] = (uint8_t)(DATA_ATTR_SAMPLE_RATE >> 24);
    
    {
        uint64_t sample_time = DATA_ATTR_SAMPLE_TIME_US;
        send_buf[12] = (uint8_t)(sample_time);
        send_buf[13] = (uint8_t)(sample_time >> 8);
        send_buf[14] = (uint8_t)(sample_time >> 16);
        send_buf[15] = (uint8_t)(sample_time >> 24);
        send_buf[16] = (uint8_t)(sample_time >> 32);
        send_buf[17] = (uint8_t)(sample_time >> 40);
    }
    
    send_buf[18] = DATA_ATTR_CHANNEL_NUM;
    /* 量程：ADS8685 RANGE_SEL 寄存器值，0x04 = ±2.56V */
    send_buf[19] = ADS8685_RANGE_2V56;  /* 0x04 */
    memset(&send_buf[20], 0, 12);  /* 剩余保留区清零 */
    
    HAL_UART_Transmit(&hlpuart1, send_buf, DATA_FRAME_HEADER_LEN, 60000U);
    checksum = uart_calc_checksum(send_buf, DATA_FRAME_HEADER_LEN);

    /* ---- 电压数据 ---- */
    for (i = 0; i < SAMPLE_COUNT; i++) {
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U]     = (uint8_t)(voltage_buf[i]);
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U + 1U] = (uint8_t)(voltage_buf[i] >> 8);

        if ((i + 1U) % (TX_CHUNK_SIZE / 2U) == 0 || i == SAMPLE_COUNT - 1U) {
            chunk_bytes = ((i % (TX_CHUNK_SIZE / 2U)) + 1U) * 2U;
            HAL_UART_Transmit(&hlpuart1, send_buf, chunk_bytes, HAL_MAX_DELAY);
            checksum ^= uart_calc_checksum(send_buf, chunk_bytes);
        }
    }

    /* ---- 电流数据 ---- */
    for (i = 0; i < SAMPLE_COUNT; i++) {
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U]     = (uint8_t)(current_buf[i]);
        send_buf[(i % (TX_CHUNK_SIZE / 2U)) * 2U + 1U] = (uint8_t)(current_buf[i] >> 8);

        if ((i + 1U) % (TX_CHUNK_SIZE / 2U) == 0 || i == SAMPLE_COUNT - 1U) {
            chunk_bytes = ((i % (TX_CHUNK_SIZE / 2U)) + 1U) * 2U;
            HAL_UART_Transmit(&hlpuart1, send_buf, chunk_bytes, HAL_MAX_DELAY);
            checksum ^= uart_calc_checksum(send_buf, chunk_bytes);
        }
    }

    HAL_UART_Transmit(&hlpuart1, &checksum, 1U, HAL_MAX_DELAY);
    evt_tx_done = 1;
}