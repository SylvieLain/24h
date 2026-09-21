#include <stdint.h>
#include "ti_msp_dl_config.h"
#include "app/tasks/task.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "Hardware/TB6612/bsp_tb6612.h"

volatile uint8_t TASK = 0U;
volatile TraceState trace_state = OPEN;

#define TASK1_OPEN_LOOP_PWM 2000
static uint8_t task1_started = 0U;
static TraceState previous_state = OPEN;

static TraceState read_trace_state(void)
{
    const uint32_t pins[] = {Track_T1_PIN, Track_T2_PIN, Track_T3_PIN, Track_T4_PIN,
                              Track_T5_PIN, Track_T6_PIN, Track_T7_PIN, Track_T8_PIN};
    GPIO_Regs *ports[] = {Track_T1_PORT, Track_T2_PORT, Track_T3_PORT, Track_T4_PORT,
                          Track_T5_PORT, Track_T6_PORT, Track_T7_PORT, Track_T8_PORT};
    uint8_t black_count = 0U;
    for (uint8_t i = 0U; i < 8U; i++)
    {
        if (DL_GPIO_readPins(ports[i], pins[i]) == 0U) black_count++;
    }
    return (black_count >= 2U) ? CLOSE : OPEN;
}

void open_loop1(void)
{
    Motor_SetPWM(1U, TASK1_OPEN_LOOP_PWM);
    Motor_SetPWM(2U, TASK1_OPEN_LOOP_PWM);
}

void open_loop2(void)
{
    TB6612_Motor_Stop();
}

void task_loop(void)
{
    trace_state = read_trace_state();
    trace_status = (trace_state == CLOSE) ? 1U : 0U;

    switch (TASK)
    {
    case 0U:
        task1_started = 0U;
        previous_state = trace_state;
        open_loop2();
        break;

    case 1U:
        if (task1_started == 0U)
        {
            /* A 点为 CLOSE，按键启动后立即开环驶出 A 点。 */
            task1_started = 1U;
            previous_state = trace_state;
            open_loop1();
            break;
        }

        if ((previous_state == OPEN) && (trace_state == CLOSE))
        {
            open_loop2();
            TASK = 0U;
            task1_started = 0U;
            Buzzer_for_a_while();
        }
        else
        {
            open_loop1();
        }
        previous_state = trace_state;
        break;

    default:
        TASK = 0U;
        task1_started = 0U;
        previous_state = trace_state;
        open_loop2();
        break;
    }
}
