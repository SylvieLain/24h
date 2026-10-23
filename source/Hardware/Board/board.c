

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "Hardware/User/User.h"
#include "board.h"
#include "ti/driverlib/m0p/dl_core.h"


int fputc(int ch, FILE *f)
{
    (void)f;
    return ch;
}

/**
 * @brief 输出带文件/函数/行号信息的调试日志。
 *
 * 函数会格式化日志前缀，附加用户提供的消息，然后通过 UART0 发送。
 *
 * @param __file 源文件名，通常传入 __FILE__。
 * @param __func 函数名，通常传入 __func__。
 * @param __line 源代码行号，通常传入 __LINE__。
 * @param format printf 风格格式字符串。
 * @param ... 其它格式参数。
 * @return vsnprintf 返回的写入字符数。
 */
int LOG_Debug_Out(const char* __file, const char* __func, int __line, const char* format, ...)
{
    (void)__file; (void)__func; (void)__line; (void)format;
    return 0;
}

/**
 * @brief 通过 UART0 打印格式化字符串。
 *
 * 该函数类似 printf，但不会附加文件/函数/行号信息，直接发送到调试串口。
 *
 * @param format printf 风格格式字符串。
 * @param ... 其它格式参数。
 * @return vsnprintf 返回的写入字符数。
 */
int lc_printf(char* format,...)
{
    (void)format;
    return 0;
}


/* ================ 延时函数封装 =================== */

/**
 * @brief 延时指定的微秒数。
 *
 * 通过 CPU 时钟循环计数实现忙等待延时，遇到中断时会延长实际时间。
 */
void delay_us(int __us) { delay_cycles((CPUCLK_FREQ / 1000 / 1000) * __us); }

/**
 * @brief 延时指定的毫秒数。
 */
void delay_ms(int __ms) { delay_cycles((CPUCLK_FREQ / 1000) * __ms); }

/**
 * @brief delay_us 的别名。
 *
 * 该函数用于兼容使用 _1us/_1ms 命名风格的代码。
 */
void delay_1us(int __us) { delay_cycles((CPUCLK_FREQ / 1000 / 1000) * __us); }

/**
 * @brief delay_ms 的别名。
 */
void delay_1ms(int __ms) { delay_cycles((CPUCLK_FREQ / 1000) * __ms); }


/* ================ UART1 接口函数 =================== */

/**
 * @brief 初始化 UART1 并使能接收中断。
 *
 * 在 SysConfig 已完成硬件配置的基础上，额外使能接收 FIFO 中断
 * 并打开 NVIC 中断。视觉球链路已移出构建，中断处理仅清空 FIFO。
 */
void uart1_init(void)
{
    /* 清空 RX FIFO 中所有残留数据 */
    while (DL_UART_Main_isRXFIFOEmpty(UART_1_INST) == false) {
        DL_UART_Main_receiveData(UART_1_INST);
    }
    DL_UART_Main_clearInterruptStatus(UART_1_INST, DL_UART_MAIN_INTERRUPT_RX);
    DL_UART_Main_enableInterrupt(UART_1_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_EnableIRQ(UART_1_INST_INT_IRQN);
}

/**
 * @brief 通过 UART1 阻塞发送一个字节。
 *
 * @param dat 要发送的字节。
 */
void uart1_sendChar(uint8_t dat)
{
    /* [Brain改] 原来的 DL_UART_isBusy() 同时受RX活动影响，视觉数据流持续
       到来时会永久卡死；只等TX FIFO可写。提醒：此处原实现有隐患。 */
    while (DL_UART_Main_isTXFIFOFull(UART_1_INST) == true)
    {
        ;
    }
    DL_UART_Main_transmitData(UART_1_INST, dat);
}

/**
 * @brief 通过 UART1 发送以 \0 结尾的字符串。
 *
 * @param str 要发送的字符串。
 */
void uart1_sendString(char* str)
{
    while (str != NULL && *str != 0)
    {
        uart1_sendChar((uint8_t)*str++);
    }
}

volatile uint32_t Tick = 0;

void SysTick_Handler(void) {
    Tick_SysTickCallback();
}
void Tick_delay(uint32_t t) {
    uint32_t tEnd = Tick + t;
    /* [Brain改] 用有符号差判断，Tick回绕时不会永久卡死 */
    while ((int32_t)(Tick - tEnd) < 0)
    {
        ;
    }
}
/* SysTick中断回调(1ms) —— 本工程主节拍由 TIMG0 每10ms驱动，此函数仅备用 */
void Tick_SysTickCallback(void) {
    Tick++;
}

/**
 * @brief UART1 中断处理函数。
 *
 * 视觉球（BallLink）链路已移出构建，本处理函数不再解析协议，只做两件事：
 *   1) 读取并清除 UART1 挂起中断；
 *   2) 排空 RX FIFO，避免残留数据持续触发中断造成中断风暴。
 * UART1 接收中断保持使能，后续接入新链路时再在此补充解析。
 */
void UART1_IRQHandler(void)
{
    (void)DL_UART_Main_getPendingInterrupt(UART_1_INST);
    while (DL_UART_Main_isRXFIFOEmpty(UART_1_INST) == false)
    {
        (void)DL_UART_Main_receiveData(UART_1_INST);
    }
}
