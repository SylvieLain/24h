#include <stdint.h>
#include "ti_msp_dl_config.h"
#include "app/tasks/task.h"
#include "middle/mid_pid.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Board/board.h"
#include "Hardware/jy901s/jy901s.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "Hardware/TB6612/bsp_tb6612.h"

#define TASK1_BASE_PWM 2000.0f
#define TASK1_PD_PERIOD_MS 10U
#define TASK2_ARC_MIN_TURN_DEG 120.0f

volatile uint8_t TASK = 0U;
volatile uint8_t car_pos = 0;//0-a 1-b 2-c 3-d
volatile TraceState trace_state = OPEN;



static uint8_t task1_started = 0U;
static uint8_t task2_started = 0U;
static TraceState previous_state = OPEN;
static uint32_t last_pd_tick = 0U;
static float arc_start_yaw = 0.0f;

/*获取运行环状态
若循迹>=1则鉴定为闭环*/
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
    return (black_count >= 1U) ? CLOSE : OPEN;
}

void open_loop1(void)
{
    Motor_SetPWM(1U, (int32_t)TASK1_BASE_PWM);
    Motor_SetPWM(2U, (int32_t)TASK1_BASE_PWM);
}

/*待定任务34的斜线控制开环*/
void open_loop2(void)
{

}

/* OPEN 段每 10 ms 用 Z 角误差更新已有转向 PD 的电机输出。 */
static void task1_hold_heading(void)
{
    const uint32_t now = Tick;
    if ((uint32_t)(now - last_pd_tick) < TASK1_PD_PERIOD_MS) return;
    last_pd_tick = now;

    float yaw = Gyro_Data.angle_z;
    const float delta = yaw - turn_PID.Target;
    if (delta > 180.0f) yaw -= 360.0f;
    if (delta < -180.0f) yaw += 360.0f;

    turn_PID.Actual = yaw;
    PID_Update(&turn_PID);
    LeftPWM = TASK1_BASE_PWM - turn_PID.Out;
    RightPWM = TASK1_BASE_PWM + turn_PID.Out;
    Motor_SetPWM(1U, (int32_t)LeftPWM);
    Motor_SetPWM(2U, (int32_t)RightPWM);
}

static uint8_t begin_heading_hold(void)
{
    if (Gyro_UpdateFlag == 0U) return 0U;
    turn_PID.Target = Gyro_Data.angle_z;
    turn_PID.Actual = turn_PID.Target;
    turn_PID.Actual1 = turn_PID.Target;
    turn_PID.Error0 = 0.0f;
    turn_PID.Error1 = 0.0f;
    turn_PID.ErrorInt = 0.0f;
    last_pd_tick = Tick;
    return 1U;
}

static uint8_t arc_finished(void)
{
    float delta = Gyro_Data.angle_z - arc_start_yaw;
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    if (delta < 0.0f) delta = -delta;
    return (delta >= TASK2_ARC_MIN_TURN_DEG) ? 1U : 0U;
}

void task_loop(void)
{
    trace_state = read_trace_state();
    trace_status = (trace_state == CLOSE) ? 1U : 0U;

    switch (TASK)
    {
    case 0U:
    /*默认任务为0待定 并失能电机*/
        task1_started = 0U;
        task2_started = 0U;
        previous_state = trace_state;
        TrackFlag = 0U;
        TB6612_Motor_Stop();
        break;

    case 1U:
        if (task1_started == 0U)
        {
            car_pos=0;
            /* 按键只提交请求转换任务*/
            if (trace_state != CLOSE)
            {
                TASK = 0U;
                previous_state = trace_state;
                TB6612_Motor_Stop();
                break;
            }
            task1_started = 1U;
            previous_state = CLOSE;
            TrackFlag = 0U;
            open_loop1();
            break;
        }

        if ((previous_state == CLOSE) && (trace_state == OPEN))
        {
            if (begin_heading_hold() == 0U)
            {
                TASK = 0U;
                TB6612_Motor_Stop();
                break;
            }
            previous_state = OPEN;
        }
        else if ((previous_state == OPEN) && (trace_state == CLOSE))
        {
            TB6612_Motor_Stop();
            TASK = 0U;
            task1_started = 0U;
            Buzzer_for_a_while();
        }
        else if (trace_state == OPEN)
        {
            task1_hold_heading();
        }
        break;

    case 2U:
        if (task2_started == 0U)
        {
            if (trace_state != CLOSE)
            {
                TASK = 0U;
                TB6612_Motor_Stop();
                break;
            }
            task2_started = 1U;
            car_pos = 0U;
            previous_state = CLOSE;
            TrackFlag = 0U;
            open_loop1();
            break;
        }

        if ((previous_state == CLOSE) && (trace_state == OPEN))
        {
            if (car_pos == 3U)
            {
                if (arc_finished() != 0U) Buzzer_for_a_while();
                car_pos = 0U;
                TASK = 0U;
                TrackFlag = 0U;
                TB6612_Motor_Stop();
                break;
            }
            if ((car_pos == 1U) && (arc_finished() == 0U))
            {
                TASK = 0U;
                TrackFlag = 0U;
                TB6612_Motor_Stop();
                break;
            }
            if (car_pos == 1U)
            {
                car_pos = 2U;
                Buzzer_for_a_while();
                TrackFlag = 0U;
            }
            if (begin_heading_hold() == 0U)
            {
                TASK = 0U;
                TB6612_Motor_Stop();
                break;
            }
            previous_state = OPEN;
        }
        else if ((previous_state == OPEN) && (trace_state == CLOSE))
        {
            if (car_pos == 0U || car_pos == 2U)
            {
                car_pos = (car_pos == 0U) ? 1U : 3U;
                arc_start_yaw = Gyro_Data.angle_z;
                TrackFlag = 1U;
                Buzzer_for_a_while();
            }
            previous_state = CLOSE;
        }
        else if (trace_state == OPEN)
        {
            task1_hold_heading();
        }
        break;

    default:
        TASK = 0U;
        task1_started = 0U;
        previous_state = trace_state;
        TB6612_Motor_Stop();
        break;
    }
}
