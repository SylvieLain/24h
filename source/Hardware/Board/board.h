
#ifndef __BOARD_H__
#define __BOARD_H__

/**
 * @file board.h
 * @brief UART0/1的 调试和延时辅助函数的板级支持头文件。
 *
 * 本头文件对外提供轻量级日志输出函数和忙等待延时原语。
 */

#include "ti_msp_dl_config.h"

#ifndef u8
#define u8 uint8_t
#endif

#ifndef u16
#define u16 uint16_t
#endif

#ifndef u32
#define u32 uint32_t
#endif

#ifndef u64
#define u64 uint64_t
#endif

/**
 * @brief 输出带文件/函数/行号信息的调试日志。
 *
 * 通常通过 LOG_D 宏自动添加源位置信息。
 */
int LOG_Debug_Out(const char* __file, const char* __func, int __line, const char* format, ...);

/**
 * @brief 自动捕获源文件、函数和行号的日志宏。
 *
 * 示例：
 *   LOG_D("value=%d", value);
 */
#define LOG_D(fmt, ...) \
    do { \
        LOG_Debug_Out(__FILE__, (const char*)__func__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief 直接向 UART 调试通道输出格式化字符串。
 *
 * 行为类似 printf，但不附加源位置信息。
 */
int lc_printf(char* format,...);

/**
 * @brief 延时指定的微秒数。
 */
void delay_us(int __us);

/**
 * @brief 延时指定的毫秒数。
 */
void delay_ms(int __ms);

/**
 * @brief delay_us 的别名。
 */
void delay_1us(int __us);

/**
 * @brief delay_ms 的别名。
 */
void delay_1ms(int __ms);

/* ================ UART1 接口函数 =================== */

/**
 * @brief 初始化 UART1 并使能接收中断。
 */
void uart1_init(void);

/**
 * @brief 通过 UART1 阻塞发送一个字节。
 */
void uart1_sendChar(uint8_t dat);

/**
 * @brief 通过 UART1 发送以 \0 结尾的字符串。
 */
void uart1_sendString(char* str);

/* [Brain改] 原有的 uart1_rx_buffer/uart1_rx_len/uart1_rx_flag/uart1_rx_raw/
   UART1_GetParsedData 属于旧云台 0xFF 协议的接口，已随协议替换移除，
   解析结果统一由 middle/mid_ball_link.h 的 BallLink_* 变量提供。提醒。 */

extern volatile uint32_t Tick;
void Tick_delay(uint32_t t);
void Tick_SysTickCallback(void);
void SysTick_Handler(void);

void uart2_init(void);
void uart3_init(void);

void UART1_IRQHandler(void);
void UART2_IRQHandler(void);
void UART3_IRQHandler(void);


#endif
