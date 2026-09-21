#include <ti_msp_dl_config.h>
#include "B&L.h"

/*
 * 无源蜂鸣器驱动说明（PA11，经三极管 Q2）：
 * - PA11 在 MSPM0G3507 上的定时器复用只有 TIMA1_CCP1 与 TIMA0_CCP2_CMPL
 *  （见 libraries/ti/devices/msp/m0p/mspm0g350x.h 中 IOMUX_PINCM22 定义），
 *   两者分别与电机 PWM（TIMA1/CCP0=PB2、CCP1=PB3）及 5ms 周期定时器
 *   （TIMER_1=TIMA0）冲突，故 PA11 无“不冲突”的硬件 PWM 通道。
 * - 因此音调采用空闲定时器 TIMG8（SysConfig 未使用：已用 TIMA1/TIMG0/
 *   TIMA0/TIMG6，TIMG7/TIMG8/TIMG12 空闲）做 0.25ms 周期中断，
 *   在蜂鸣有效期内翻转 PA11（GPIO），得到 2kHz/50% 方波；TIMG8 为本模块
 *   代码独占，不改 .syscfg 与生成文件，避免下次 SysConfig 生成被覆盖。
 * - 时长门控仍由 Buzzer_Service_10ms()（TIMG0 10ms 中断调用）非阻塞完成；
 *   任务1接口与状态机不变。
 */

static volatile uint16_t buzzer_ticks_10ms = 0U;

/* 音调时基：TIMG8，BUSCLK=80MHz。 */
#define BUZZER_TONE_INST (TIMG8)
#define BUZZER_TONE_INT_IRQn (TIMG8_INT_IRQn)
/*
 * 分频 8 得 10MHz；每 0.25ms 中断翻转一次，得周期 0.5ms = 2kHz。
 * LOAD = 0.25ms * 10MHz - 1 = 2500 - 1 = 2499。
 */
#define BUZZER_TONE_LOAD (2499U)

static void Buzzer_ToneTimer_Init(void)
{
    static const DL_TimerG_ClockConfig toneClockConfig = {
        .clockSel    = DL_TIMER_CLOCK_BUSCLK,
        .divideRatio = DL_TIMER_CLOCK_DIVIDE_8,
        .prescale    = 0U,
    };
    static const DL_TimerG_TimerConfig toneTimerConfig = {
        .period     = BUZZER_TONE_LOAD,
        .timerMode  = DL_TIMER_TIMER_MODE_PERIODIC,
        .startTimer = DL_TIMER_START,
    };

    DL_TimerG_reset(BUZZER_TONE_INST);
    DL_TimerG_enablePower(BUZZER_TONE_INST);
    DL_TimerG_setClockConfig(BUZZER_TONE_INST,
        (DL_TimerG_ClockConfig *)&toneClockConfig);
    DL_TimerG_initTimerMode(BUZZER_TONE_INST,
        (DL_TimerG_TimerConfig *)&toneTimerConfig);
    DL_TimerG_enableInterrupt(BUZZER_TONE_INST,
        DL_TIMERG_INTERRUPT_ZERO_EVENT);
    NVIC_SetPriority(BUZZER_TONE_INT_IRQn, 3);
    NVIC_EnableIRQ(BUZZER_TONE_INT_IRQn);
    DL_TimerG_enableClock(BUZZER_TONE_INST);
    DL_TimerG_startCounter(BUZZER_TONE_INST);
}

void BL_Init(void)
{
    buzzer_ticks_10ms = 0U;
    DL_GPIO_enableOutput(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
    DL_GPIO_clearPins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
    Buzzer_ToneTimer_Init();
}

void Buzzer_ON(void)     { DL_GPIO_setPins   (Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }
void Buzzer_OFF(void)    { DL_GPIO_clearPins (Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }
void Buzzer_Toggle(void) { DL_GPIO_togglePins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }

void Buzzer_Beep(uint16_t duration_ms)
{
    buzzer_ticks_10ms = (uint16_t)((duration_ms + 9U) / 10U);
    if (buzzer_ticks_10ms == 0U)
    {
        buzzer_ticks_10ms = 1U;
    }
    /* 定相从低电平起振，首个 0.25ms 节拍后输出高，音调由 TIMG8 中断翻转产生。 */
    DL_GPIO_clearPins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
}

void Buzzer_Service_10ms(void)
{
    if (buzzer_ticks_10ms == 0U)
    {
        return;
    }
    buzzer_ticks_10ms--;
    if (buzzer_ticks_10ms == 0U)
    {
        Buzzer_OFF();
    }
}

void Buzzer_for_a_while(void)
{
    Buzzer_Beep(80U);
}

void BL_ON(void)     { Buzzer_ON();     }
void BL_OFF(void)    { Buzzer_OFF();    }
void BL_Toggle(void) { Buzzer_Toggle(); }

void TIMG8_IRQHandler(void)
{
    switch (DL_TimerG_getPendingInterrupt(BUZZER_TONE_INST))
    {
    case DL_TIMER_IIDX_ZERO:
        /* 仅在定时蜂鸣有效期内翻转，空闲时保持 GPIO 原状态（不干扰手动电平）。 */
        if (buzzer_ticks_10ms != 0U)
        {
            DL_GPIO_togglePins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
        }
        break;
    default:
        break;
    }
}
