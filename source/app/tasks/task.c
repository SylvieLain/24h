#include <stdint.h>
#include "ti_msp_dl_config.h"
#include "app/tasks/task.h"
#include "middle/mid_pid.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Board/board.h"
#include "Hardware/jy901s/jy901s.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "Hardware/TB6612/bsp_tb6612.h"



/* TASK 由按键提交  */
volatile uint8_t TASK = 0U;
volatile uint8_t car_pos = CAR_POS_A;
volatile TraceState trace_state = OPEN;

/* 只保留任务入口、探头边沿和控制周期三个内部记录。 */
static uint8_t running_task = 0U;
static TraceState previous_state = OPEN;
static uint32_t last_pd_tick = 0U;
/* 任务3首段手动对准 A→C 后锁定的航向，仅用于首段保持。 */
static float task3_ac_heading = 0.0f;

/* 获取trace_state */
static TraceState read_trace_state(void)
{
    const uint32_t pins[] = {
        Track_T1_PIN, Track_T2_PIN, Track_T3_PIN, Track_T4_PIN,
        Track_T5_PIN, Track_T6_PIN, Track_T7_PIN, Track_T8_PIN
    };
    GPIO_Regs *ports[] = {
        Track_T1_PORT, Track_T2_PORT, Track_T3_PORT, Track_T4_PORT,
        Track_T5_PORT, Track_T6_PORT, Track_T7_PORT, Track_T8_PORT
    };

    /* 线可能只覆盖一路探头：至少一路低电平即认为见线。 */
    for (uint8_t i = 0U; i < 8U; i++)
    {
        if (DL_GPIO_readPins(ports[i], pins[i]) == 0U) return CLOSE;
    }
    return OPEN;
}

/*反转,陀螺仪安装问题*/
static float wrap_heading(float angle)
{
    if (angle > 180.0f) angle -= 360.0f;
    if (angle < -180.0f) angle += 360.0f;
    return angle;
}

/*急停*/
static void stop_task(void)
{
    TASK = 0U;
    running_task = 0U;
    TrackFlag = 0U;
    TB6612_Motor_Stop();
}


static uint8_t begin_heading_hold(float target, float output_limit)
{
    if (Gyro_UpdateFlag == 0U) return 0U;

    turn_PID.Target = wrap_heading(target);
    turn_PID.Actual = turn_PID.Target +
                      wrap_heading(Gyro_Data.angle_z - turn_PID.Target);

    turn_PID.Actual1 = turn_PID.Actual;
    turn_PID.Error0 = turn_PID.Target - turn_PID.Actual;
    turn_PID.Error1 = turn_PID.Error0;
    turn_PID.ErrorInt = 0.0f;
    turn_PID.Out    = 0.0f;
    turn_PID.OutMax =  output_limit;
    turn_PID.OutMin = -output_limit;
    last_pd_tick = Tick;
    return 1U;
}

/* 每10 ms闭环更新；Motor_SetPWM 写的是 PD 算出的执行量。 */
static void update_heading_hold(uint8_t immediate)
{
    const uint32_t now = Tick;
    if ((immediate == 0U) &&
        ((uint32_t)(now - last_pd_tick) < HEADING_PD_PERIOD_MS)) return;
    last_pd_tick = now;

    turn_PID.Actual = turn_PID.Target +
                      wrap_heading(Gyro_Data.angle_z - turn_PID.Target);
    PID_Update(&turn_PID);

    /*任务3特调*/
    if ((running_task == 3U) && (car_pos == CAR_POS_B))
    {
        /* B 出弯加强比例转向；差量限在基础 PWM 内，内侧不反转。 */
        turn_PID.Out += turn_PID.Kp * turn_PID.Error0;
        if (turn_PID.Out > TASK_BASE_PWM) turn_PID.Out = TASK_BASE_PWM;
        if (turn_PID.Out < -TASK_BASE_PWM) turn_PID.Out = -TASK_BASE_PWM;
    }

    /* 正输出：左侧减、右侧加；电机1/2分别对应左/右 */
    LeftPWM  = TASK_BASE_PWM -turn_PID.Out;
    RightPWM = TASK_BASE_PWM + turn_PID.Out;

    if ((running_task == 3U) && (car_pos == CAR_POS_B))
    {
        /* 只给外侧轮补速：满差量时内侧0、外侧9999；误差归零后回到双侧3000。 */
        const float boost = (PD_LIMIT - 2.0f * TASK_BASE_PWM) *
                            (turn_PID.Out / TASK_BASE_PWM);
        if (boost > 0.0f) RightPWM += boost;
        if (boost < 0.0f) LeftPWM  -= boost;
    }

    Motor_SetPWM(1U, (int32_t) LeftPWM);
    Motor_SetPWM(2U, (int32_t)RightPWM);
}

/*任务12直线开环*/
void open_loop1(void)
{
    Motor_SetPWM(1U, (int32_t)TASK_BASE_PWM);
    Motor_SetPWM(2U, (int32_t)TASK_BASE_PWM);
}

/* 任务3对角开环*/
void open_loop2(void)
{
    update_heading_hold(0U);
}

/* 手动放在 A：任务1/2朝水平直线；任务3已对准 A→C 对角线。 */
static void start_task(uint8_t task)
{
    if (trace_state != CLOSE)
    {
        stop_task();
        return;
    }

    running_task = task;
    car_pos = CAR_POS_A;
    previous_state = CLOSE;
    TrackFlag = 0U;
    turn_PID.OutMax = PD_LIMIT;
    turn_PID.OutMin = -PD_LIMIT;
    if (task == 3U)
    {
        task3_ac_heading = wrap_heading(Gyro_Data.angle_z);
        if (begin_heading_hold(0, PD_LIMIT) == 0U)
        {
            stop_task();
            return;
        }
        open_loop2();
    }
    else
    {
        open_loop1();
    }
}

static void run_task1(void)
{
    if ((previous_state == CLOSE) && (trace_state == OPEN))
    {
        /* 离开 A：用此刻 Z 角作为 A→B 的直线目标。 */
        if (begin_heading_hold(Gyro_Data.angle_z, PD_LIMIT) == 0U)
            stop_task();
    }
    else if ((previous_state == OPEN) && (trace_state == CLOSE))
    {
        /* 到 B：任务1结束。 */
        car_pos = CAR_POS_B;
        Buzzer_for_a_while();
        stop_task();
    }
    else if (trace_state == OPEN)
    {
        update_heading_hold(0U);
    }
}

static void run_task2(void)
{
    if ((previous_state == CLOSE) && (trace_state == OPEN))
    {
        /* 出线：A驶出、C出弯、A返回，按最近经过的位置分开处理。 */
        switch (car_pos)
        {
        case CAR_POS_A:
            if (begin_heading_hold(Gyro_Data.angle_z, PD_LIMIT) == 0U)
                stop_task();
            break;

        case CAR_POS_B:
        {
            /* B→C弧线结束：只在 C→D 目标上叠加用户的 CD_AMEND。 */
            const float cd_heading =
                wrap_heading(turn_PID.Target + 180.0f + OPEN_LOOP1_AMEND);
            car_pos = CAR_POS_C;
            TrackFlag = 0U;
            Buzzer_for_a_while();
            if (begin_heading_hold(cd_heading, PD_LIMIT) == 0U)
                stop_task();
            break;
        }

        case CAR_POS_D:
            /* D→A弧线结束：任务2唯一的正常结束点。 */
            car_pos = CAR_POS_A;
            Buzzer_for_a_while();
            stop_task();
            break;

        default:
            break;
        }
    }
    else if ((previous_state == OPEN) && (trace_state == CLOSE))
    {
        /* 入线：B或D，循迹输出由 main.c 的10 ms定时中断接管。 */
        switch (car_pos)
        {
        case CAR_POS_A:
            car_pos = CAR_POS_B;
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        case CAR_POS_C:
            car_pos = CAR_POS_D;
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        default:
            break;
        }
    }
    else if (trace_state == OPEN)
    {
        update_heading_hold(0U);
    }
}

static void run_task3(void)
{
    if ((previous_state == CLOSE) && (trace_state == OPEN))
    {
        switch (car_pos)
        {
        case CAR_POS_A:
            /* 首次驶出 A */
            break;

        case CAR_POS_C:
        {
            const float exit_heading = Gyro_Data.angle_z;/*出门采样*/

            const float bd_heading = wrap_heading(
                exit_heading + OPEN_LOOP2_AMEND);

            car_pos = CAR_POS_B;
            TrackFlag = 0U;
            Buzzer_for_a_while();
            
            if (begin_heading_hold(bd_heading, PD_LIMIT) == 0U)
                stop_task();
            else
                update_heading_hold(1U);
            break;
        }

        case CAR_POS_D:
            /* D→A弧线结束：任务3一圈完成；任务4的多圈另行接入。 */
            car_pos = CAR_POS_A;
            Buzzer_for_a_while();
            stop_task();
            break;

        default:
            break;
        }
    }
    else if ((previous_state == OPEN) && (trace_state == CLOSE))
    {
        switch (car_pos)
        {
        case CAR_POS_A:
            /* A→C对角线到 C，启用 C→B 半弧循迹。 */
            car_pos = CAR_POS_C;
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        case CAR_POS_B:
            /* B→D对角线到 D，启用 D→A 半弧循迹。 */
            car_pos = CAR_POS_D;
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        default:
            break;
        }
    }
    else if (TrackFlag == 0U)
    {
        /* 启动时也需在 A 的黑线上输出，才能驶入无黑线的对角段。 */
        open_loop2();
    }
}

/* 主循环只做：采样 → 任务入口/分发 → 保存本次探头状态。 */
void task_loop(void)
{
    const uint8_t requested_task = TASK;
    trace_state = read_trace_state();
    trace_status = (trace_state == CLOSE) ? 1U : 0U;

    if ((requested_task != 1U) && (requested_task != 2U) && (requested_task != 3U))
    {
        stop_task();
    }
    else if (running_task != requested_task)
    {
        start_task(requested_task);
    }
    else
    {
        switch (requested_task)
        {
        case 1U:
            run_task1();
            break;
        case 2U:
            run_task2();
            break;
        case 3U:
            run_task3();
            break;
        }
    }
    previous_state = trace_state;
}
