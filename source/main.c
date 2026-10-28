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
#include "app/heading/app_heading.h"



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
    Heading_Init();
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
    switch (DL_TimerG_getPendingInterrupt(TIMER_0_INST))
    {
    case DL_TIMER_IIDX_ZERO:
        flex_button_scan();
        Buzzer_IRQService();

        /* 循迹环 */
        if (((TASK == 2U) || (TASK == 3U) || (TASK == 4U)) &&
            (TrackFlag != 0U))
        {

            LineWalking();
            DifPWM = LineTracking_ComputeDuty(task_speed, TASK);
            LeftPWM =  task_speed + DifPWM;
            RightPWM = task_speed - DifPWM;

            Motor_SetPWM(1U, (int32_t)LeftPWM);
            Motor_SetPWM(2U, (int32_t)RightPWM);
        }
        else
        {
            LineTracking_ResetPd();
            if (TASK == 0U) TB6612_Motor_Stop();
        }

        /* 航向环：内部检查任务归属与使能，和循迹输出互斥。 */
        update_heading(0U);


        /*  编码器
            每40 ms更新速度观测 */
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
