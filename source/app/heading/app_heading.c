#include "app_heading.h"
#include "app/tasks/task.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Board/board.h"
#include "Hardware/jy901s/jy901s.h"
#include "Hardware/TB6612/bsp_tb6612.h"
#include <math.h>

PID_t turn_PID;

/* 0 为停用；记录所属任务，按键切换后不执行上一任务的航向输出。 */
static volatile uint8_t heading_task = 0U;

void Heading_Init(void)
{
    heading_task = 0U;
    PID_Init(&turn_PID);
    turn_PID.Kp = 550.0f;
    turn_PID.Ki = 0.0f;
    turn_PID.Kd = 0.0f;
}

void Heading_SetTask(uint8_t task)
{
    /* 目标、PID历史及阶段标志先准备好，再向定时中断发布使能。 */
    __DMB();
    heading_task = task;
}

/*
任务3/4 的航向保持执行器 + B 点“对准→稳定→巡航”三态状态机。
任务1/2直线段也复用；周期调用只来自10 ms定时中断。 */
void update_heading(uint8_t immediate)
{
    uint32_t timer_enabled = 0U;
    if (immediate != 0U)
    {
        /* 主循环只在启动/阶段切换时立即更新，避免被周期计算抢占。
         * 仅屏蔽控制定时器，UART接收等更高优先级中断继续运行。 */
        timer_enabled = NVIC_GetEnableIRQ(TIMER_0_INST_INT_IRQN);
        NVIC_DisableIRQ(TIMER_0_INST_INT_IRQN);
    }

    if ((heading_task != 0U) && (TASK == heading_task) && (TrackFlag == 0U))
    {
        /* ① 稳定窗口到期：settling 持续满 25 ms → 退出稳定态。
         *    主循环检查到期后传immediate=1，不等下个10 ms节拍。 */
        if (task34_b_settling &&
            ((uint32_t)(Tick - bd_aligned_tick) >= TASK34_B_SETTLE_MS))
        {
            task34_b_settling = 0U;
        }

        /* ② 10 ms节拍由定时中断保证，不再在这里按Tick二次节流。
         *    last_pd_tick只记录实际计算时刻；立即更新不重排硬件节拍。 */
        last_pd_tick = Tick;

        /* ③ 航向环。Actual = Target + wrap(z - Target)：把误差折到 ±180°
         *    → PID_Update 内部 Error0 = Target - Actual（已折角） */
        const float z = Gyro_Data.angle_z;
        turn_PID.Actual = turn_PID.Target + wrap_heading(z - turn_PID.Target);
        PID_Update(&turn_PID);

        /* ④ B点对准判定：对准中且 |误差| <= 0.5° → 转入稳定态并记时刻 */
        if (task34_b_aligning &&
            (fabsf(turn_PID.Error0) <= TASK34_B_ALIGN_ERROR_DEG))
        {
            task34_b_aligning = 0U;
            bd_aligned_tick = Tick;
            task34_b_settling = 1U;
        }

        /* ⑤ 按阶段选输出 */
        if (task34_b_settling)       /* 停转25ms */
        {
            LeftPWM = 0.0f;
            RightPWM = 0.0f;
        }
        else if (task34_b_aligning)  /* 单轮原地转：转的那侧给 |2*Out|，另一侧0 */
        {
            LeftPWM = (turn_PID.Out < 0.0f) ? -2.0f * turn_PID.Out : 0.0f;
            RightPWM = (turn_PID.Out > 0.0f) ? 2.0f * turn_PID.Out : 0.0f;
        }
        else                        /* 巡航+纠偏 */
        {
            LeftPWM = task_speed - turn_PID.Out;
            RightPWM = task_speed + turn_PID.Out;
        }

        /* ⑥ 下发 */
        Motor_SetPWM(1U, (int32_t)LeftPWM);
        Motor_SetPWM(2U, (int32_t)RightPWM);
        if (task34_b_settling) TB6612_Motor_Stop();
    }

    if (timer_enabled != 0U) NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
}
