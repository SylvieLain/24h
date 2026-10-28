#include <stdint.h>
#include "ti_msp_dl_config.h"
#include "app/tasks/task.h"
#include "app/heading/app_heading.h"
#include "middle/mid_pid.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Board/board.h"
#include "Hardware/jy901s/jy901s.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "Hardware/TB6612/bsp_tb6612.h"

/*--------------------------------------------------------------
 * 全局状态定义
 *------------------------------------------------------------*/
volatile uint8_t	TASK				= 0U;			/* TASK 由按键提交 */
volatile uint8_t	car_pos				= CAR_POS_A;    /*描述当前小车位置*/
volatile TraceState	trace_state			= OPEN;         /*描述前后状态变化*/
volatile float		task_speed			= 3500.0f;      /*全局速度*/
volatile float		task34_a_heading	= 0.0f;         /*任务34A的航向状态*/
volatile uint8_t	task34_b_aligning	= 0U;           /*任务34B对准标志*/
volatile uint8_t	task34_b_settling	= 0U;
volatile uint32_t	last_pd_tick		= 0U;
volatile uint32_t	bd_aligned_tick		= 0U;

/*--------------------------------------------------------------
 * 模块内部状态
 *------------------------------------------------------------*/
static TraceState	previous_state		= OPEN;
static uint8_t		running_task		= 0U;
static uint8_t		task4_laps			= 0U;
static float		task4_ac_heading	= 0.0f;
static uint8_t		bd_exit_pending		= 0U;
static uint32_t		bd_exit_tick		= 0U;


/*急停*/
static void stop_task(void)
{
    Heading_SetTask(0U);
    TASK = 0U;
    running_task = 0U;
    TrackFlag = 0U;
    bd_exit_pending = 0U;
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
    LineTracking_SetEntryDirection(0);
    TB6612_Motor_Stop();
}

/* 触线后立即按路线转入，定时循迹继续完成捕线，不阻塞任务循环。 */
static void begin_diagonal_entry(int8_t direction)
{
    Heading_SetTask(0U);
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
    LineTracking_SetEntryDirection(direction);
    LeftPWM = (direction < 0) ? 0.0f : 2.0f * task_speed;
    RightPWM = (direction < 0) ? 2.0f * task_speed : 0.0f;
    Motor_SetPWM(1U, (int32_t)LeftPWM);
    Motor_SetPWM(2U, (int32_t)RightPWM);
    TrackFlag = 1U;
}

/*航向角保持*/
static uint8_t begin_heading_hold(float target, float output_limit)
{
    Heading_SetTask(0U);
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
    Heading_SetTask(running_task);
    return 1U;
}


static void begin_bd_exit_pause(void)
{
    Heading_SetTask(0U);
    TrackFlag = 0U;
    Buzzer_for_a_while();
    LeftPWM = 0.0f;
    RightPWM = 0.0f;

    Motor_SetPWM(1U, 0);
    Motor_SetPWM(2U, 0);
    TB6612_Motor_Stop();
    bd_exit_tick = Tick;
    bd_exit_pending = 1U;
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
}

static uint8_t service_bd_exit_pause(void)
{
    if (bd_exit_pending == 0U) return 0U;
    if ((uint32_t)(Tick - bd_exit_tick) < TASK3_B_EXIT_PAUSE_MS) return 1U;

    bd_exit_pending = 0U;
    /* B停顿50ms后按A基准设置固定目标；当前Z只作反馈和诊断。 
     * 图示顺时针为正，现有Z/航向环以左转为正，因此减去图示偏移。
     */
    const float target = wrap_heading(task34_a_heading - OPEN_LOOP2_AMEND);
    
    /* 对准标志先准备好，再由begin_heading_hold发布航向使能。 */
    task34_b_aligning = 1U;
    if (begin_heading_hold(target, task_speed) == 0U)
        stop_task();
    else
    {
        update_heading(1U);
    }
    return 1U;
}



/*任务12直线开环*/
void open_loop1(void)
{
    Motor_SetPWM(1U, (int32_t)task_speed);
    Motor_SetPWM(2U, (int32_t)task_speed);
}

/* 任务3对角开环*/
void open_loop2(void)
{
    /* 周期计算由定时中断执行；这里只保留25 ms到期立即前进。 */
    if ((task34_b_settling != 0U) &&
        ((uint32_t)(Tick - bd_aligned_tick) >= TASK34_B_SETTLE_MS))
    {
        update_heading(1U);
    }
}

/* 从A点启动时锁定当前航向。 */
static void start_task(uint8_t task)
{
    Heading_SetTask(0U);
    if (trace_state != OPEN)
    {
        stop_task();
        return;
    }

    bd_exit_pending     = 0U;
    task34_b_aligning   = 0U;
    task34_b_settling   = 0U;
    running_task        = task;
    car_pos             = CAR_POS_A;
    previous_state      = OPEN;
    TrackFlag           = 0U;

    LineTracking_SetEntryDirection(0);
    turn_PID.OutMax =  PD_LIMIT;
    turn_PID.OutMin = -PD_LIMIT;
    const float start_heading = Gyro_Data.angle_z;
    float heading_target = start_heading;
    if ((task == 3U) || (task == 4U))
    {
        task34_a_heading = start_heading;
        heading_target = wrap_heading(task34_a_heading);
    }
    if (task == 4U)
    {
        task4_laps = 0U;
        task4_ac_heading = heading_target;
    }
    if (begin_heading_hold(heading_target,
                           ((task == 3U) || (task == 4U)) ? task_speed : PD_LIMIT) == 0U)
    {
        stop_task();
        return;
    }
    update_heading(1U);
}

static void run_task1(void)
{
    if ((previous_state == OPEN) && (trace_state == CLOSE))
    {
        /* 到 B：任务1结束。 */
        car_pos = CAR_POS_B;
        Buzzer_for_a_while();
        stop_task();
    }
}

static void run_task2(void)
{
    if ((previous_state == CLOSE) && (trace_state == OPEN))
    {
        /* 出线：C出弯、A返回；起点A已在OPEN区，不经过此分支。 */
        switch (car_pos)
        {
        case CAR_POS_B:
        {
            /* B→C弧线结束：只在 C→D 目标上叠加用户的 CD_AMEND。 */
            const float cd_heading =
                wrap_heading(turn_PID.Target + 180.0f);
            car_pos = CAR_POS_C;
            TrackFlag = 0U;
            Buzzer_for_a_while();
            if (begin_heading_hold(cd_heading, task_speed) == 0U)
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
            Heading_SetTask(0U);
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        case CAR_POS_C:
            car_pos = CAR_POS_D;
            Heading_SetTask(0U);
            TrackFlag = 1U;
            Buzzer_for_a_while();
            break;

        default:
            break;
        }
    }
}

/* A→C定航向，C左捕线到B，BD右捕线后循迹到A；任务3一圈，任务4三圈。 */
static void run_task3(void)
{
    if (service_bd_exit_pause() != 0U) return;

    switch (car_pos)
    {
    case CAR_POS_A:
        if (trace_state == CLOSE)
        {
            car_pos = CAR_POS_C;
            begin_diagonal_entry(-1);
            Buzzer_for_a_while();
        }
        else
        {
            open_loop2();
        }
        break;

    case CAR_POS_C:
        /* 左捕线尚未完成时，全白不算B出口；定时中断继续捕线。 */
        if (LineTracking_GetEntryDirection() != 0) return;
        if ((previous_state == CLOSE) && (trace_state == OPEN))
        {
            car_pos = CAR_POS_B;
            begin_bd_exit_pause();
        }
        break;

    case CAR_POS_B:
        if ((task34_b_aligning != 0U) || (task34_b_settling != 0U))
        {
            /* 对准完成前不把扫回B弧线的探头信号认作D点。 */
            open_loop2();
        }
        else if ((previous_state == OPEN) && (trace_state == CLOSE))
        {
            car_pos = CAR_POS_D;
            begin_diagonal_entry(1);
            Buzzer_for_a_while();
        }
        else
        {
            open_loop2();
        }
        break;

    case CAR_POS_D:
        /* D点捕线完成后继续循迹；任务4前两圈回A后立即接续A→C。 */
        if (LineTracking_GetEntryDirection() != 0) return;
        if ((previous_state == CLOSE) && (trace_state == OPEN))
        {
            car_pos = CAR_POS_A;
            Buzzer_for_a_while();
            if (running_task == 4U)
            {
                task4_laps++;
                if (task4_laps < 3U)
                {
                    TrackFlag = 0U;
                    if (begin_heading_hold(task4_ac_heading, task_speed) == 0U)
                        stop_task();
                    else
                    {
                        update_heading(1U);
                    }
                }
                else
                    stop_task();
            }
            else
                stop_task();
        }
        break;

    default:
        stop_task();
        break;
    }
}

/* 主循环只做：采样 → 任务入口/分发 → 保存本次探头状态。 */
void task_loop(void)
{
    const uint8_t  requested_task = TASK;
    trace_state  = read_trace_state();

    if ((requested_task != 1U) && (requested_task != 2U) &&
        (requested_task != 3U) && (requested_task != 4U))
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
        case 4U: /* 共用单圈控制；任务4在A点计圈，第三圈结束。 */
            run_task3();
            break;
        }
    }
    previous_state = trace_state;
}
