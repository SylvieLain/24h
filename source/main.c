#include "ti_msp_dl_config.h"
#include "Hardware/Board/board.h"
#include "Hardware/LCD/lcd.h"
#include "Hardware/LCD/lcd_init.h"
#include "Hardware/TB6612/bsp_tb6612.h"
#include "Hardware/Encoder/Encoder.h"
#include "Hardware/Motor/stepper_motor.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "middle/mid_debug_led.h"
#include "middle/mid_pid.h"
#include "app/key/app_key_task.h"
#include "Hardware/BlueSerial/bsp_hc05.h"
#include <stdlib.h>
#include "Hardware/jy901s/jy901s.h"
#include "app/tarcking/app_irtracking.h"
#include "app/tasks/task.h"



static void SystemIrq_Init(void)
{
    DL_UART_Main_disableInterrupt(UART_2_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(UART_2_INST_INT_IRQN);
    NVIC_DisableIRQ(UART_2_INST_INT_IRQN);
    NVIC_ClearPendingIRQ(STEP_TIM_INST_INT_IRQN);
    NVIC_DisableIRQ(STEP_TIM_INST_INT_IRQN);

    NVIC_ClearPendingIRQ(TIMER_0_INST_INT_IRQN);
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
}

int main(void)
{
    /*初始化*/
    SYSCFG_DL_init();
    /* 使能 1ms 系统节拍，board.c 中 Tick 由 SysTick_Handler 递增 */
    SysTick_Config(CPUCLK_FREQ / 1000U);
    set_debug_led_off();
    Encoder_Init();
    TB6612_Motor_Stop();
    BL_Init();
    PID_Init(&speed_PID);
    PID_Init(&turn_PID);

        /* 航向 PID 参数*/
        turn_PID.Kp     = 550.0f;
        turn_PID.Ki     = 0.0f;
        turn_PID.Kd     = 0.0f;

    // LCD_Init();
    user_button_init();
    Gyro_Init();
    uart1_init();
    SystemIrq_Init();

    while (1)
    {
        task_loop();
    }
}

void TIMER_0_INST_IRQHandler(void)
{
    static float last_line_error = 0.0f;
    static uint8_t line_pd_active = 0U;

    switch (DL_TimerG_getPendingInterrupt(TIMER_0_INST))
    {
    case DL_TIMER_IIDX_ZERO:
        flex_button_scan();
        Buzzer_Service_10ms();

        /* 任务2沿用当前PD；任务3及Key4分段测试使用相同循迹参数。 */
        if (((TASK == 2U) || (TASK == 3U) || (TASK == 4U)) &&
            (TrackFlag != 0U))
        {
            LineWalking();
            const float error = LineTracking_GetError();
            const int8_t entry_direction = LineTracking_GetEntryDirection();
            /* 新一段循迹首拍不计算差分，避免带入上一段误差。 */
            if (line_pd_active == 0U) last_line_error = error;
            if (((TASK == 3U) || (TASK == 4U)) && (entry_direction != 0))
            {
                /* 捕线时保持已知弯向，短暂全白也不反向纠偏。 */
                DifPWM = (entry_direction < 0) ? -TASK_BASE_PWM : TASK_BASE_PWM;
                line_pd_active = 0U;
            }
            else if ((TASK == 3U) || (TASK == 4U))
            {
                DifPWM = TASK34_LINE_KP * error +
                         TASK34_LINE_KD * (error - last_line_error);
                if (DifPWM >  PD34_LIMIT) DifPWM =  PD34_LIMIT;
                if (DifPWM < -PD34_LIMIT) DifPWM = -PD34_LIMIT;
            }
            else
            {
                DifPWM = LINE_KP * error + LINE_KD * (error - last_line_error);
            }
            last_line_error = error;
            if (entry_direction == 0) line_pd_active = 1U;

            if (DifPWM >  TASK_BASE_PWM) DifPWM =  TASK_BASE_PWM;
            if (DifPWM < -TASK_BASE_PWM) DifPWM = -TASK_BASE_PWM;

            /* 误差为正表示线在右侧：左侧加、右侧减。 */
            LeftPWM =  TASK_BASE_PWM + DifPWM;
            RightPWM = TASK_BASE_PWM - DifPWM;

            Motor_SetPWM(1U, (int32_t)LeftPWM);
            Motor_SetPWM(2U, (int32_t)RightPWM);
        }
        else
        {
            line_pd_active = 0U;
            if (TASK == 0U) TB6612_Motor_Stop();
        }


        /* 每40 ms更新速度观测 */
        feedback_cnt++;
        if (feedback_cnt >= 4U)
        {
            feedback_cnt = 0;
            Encoder_UpdateSpeed();
            //转速
            LeftSpeed  = Encoder_Get(0) * 25.0f / 520.0f;
            RightSpeed = Encoder_Get(1) * 25.0f / 520.0f;

            AveSpeed = (LeftSpeed + RightSpeed) / 2.0;
        }
        break;
    default:
        break;
    }
}
