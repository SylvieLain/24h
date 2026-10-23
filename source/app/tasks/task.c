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
static   TraceState previous_state = OPEN;
static   uint8_t running_task = 0U;
volatile uint8_t task4_laps = 0U;
static float task4_ac_heading = 0.0f;
volatile float task34_a_heading = 0.0f;
volatile uint8_t task34_b_aligning = 0U;
volatile uint8_t task34_b_settling = 0U;
static uint32_t bd_aligned_tick = 0U;

static uint32_t last_pd_tick = 0U;
#define TASK3_B_EXIT_PAUSE_MS 50U
static uint8_t bd_exit_pending = 0U;
static uint32_t bd_exit_tick = 0U;

volatile CurveDebug curve_debug = {.version = 1U};
volatile CurveHeadingSample curve_heading_trace[CURVE_TRACE_SAMPLES];
volatile CurveEvent curve_events[CURVE_TRACE_EVENTS];
_Static_assert(sizeof(CurveHeadingSample) == 104U, "curve trace ABI");
_Static_assert(sizeof(CurveEvent) == 32U, "curve event ABI");

typedef struct
{
    uint32_t angle_seq, angle_tick, gyro_seq, gyro_tick;
    uint32_t checksum_errors, frame_checksums;
    float z, rate;
} CurveGyroSample;

/* UART中断发布序号在最后，避免把一帧的角度配到另一帧的时戳。 */
static CurveGyroSample curve_read_gyro(void)
{
    CurveGyroSample s;
    do
    {
        s.angle_seq = Gyro_Diag.angle_seq;
        s.gyro_seq = Gyro_Diag.gyro_seq;
        s.z = Gyro_Data.angle_z;
        s.rate = Gyro_Data.gyro_z;
        s.angle_tick = Gyro_Diag.angle_tick;
        s.gyro_tick = Gyro_Diag.gyro_tick;
        s.checksum_errors = Gyro_Diag.checksum_errors;
        s.frame_checksums = Gyro_Diag.angle_checksum_ok |
                            (Gyro_Diag.gyro_checksum_ok << 1U);
    } while ((s.angle_seq != Gyro_Diag.angle_seq) ||
             (s.gyro_seq != Gyro_Diag.gyro_seq));
    return s;
}

static void curve_event(CurvePhase phase)
{
    if (curve_debug.active == 0U) return;
    curve_debug.phase = phase;
    const uint32_t n = curve_debug.event_count;
    if (n >= CURVE_TRACE_EVENTS) return;
    const CurveGyroSample gyro = curve_read_gyro();
    volatile CurveEvent *e = &curve_events[n];
    e->tick = Tick;
    e->phase = phase;
    e->raw_z = gyro.z;
    e->gyro_z = gyro.rate;
    e->target = turn_PID.Target;
    e->left_pwm = LeftPWM;
    e->right_pwm = RightPWM;
    e->task = TASK;
    e->pos = car_pos;
    e->tracking = TrackFlag;
    e->entry = LineTracking_GetEntryDirection();
    curve_debug.event_count = n + 1U;
}

static void curve_begin(uint8_t task)
{
    const uint32_t next_run = curve_debug.run_id + 1U;
    curve_debug = (CurveDebug){.version = 1U, .run_id = next_run,
        .active = 1U, .source_task = task, .amend = OPEN_LOOP2_AMEND};
}

static void curve_record_heading(const CurveGyroSample *gyro,
                                 float previous_actual)
{
    if ((curve_debug.active == 0U) || (car_pos != CAR_POS_B) ||
        ((curve_debug.phase != CURVE_B_HEADING) &&
         (curve_debug.phase != CURVE_B_ALIGN) &&
         (curve_debug.phase != CURVE_B_SETTLE))) return;
    curve_debug.last_heading_tick = last_pd_tick;
    const uint32_t n = curve_debug.sample_count;
    if (n >= CURVE_TRACE_SAMPLES)
    {
        curve_debug.dropped++;
        return;
    }
    volatile CurveHeadingSample *s = &curve_heading_trace[n];
    s->tick = Tick;
    s->angle_seq = gyro->angle_seq;
    s->angle_tick = gyro->angle_tick;
    s->gyro_seq = gyro->gyro_seq;
    s->gyro_tick = gyro->gyro_tick;
    s->raw_z = gyro->z;
    s->gyro_z = gyro->rate;
    s->target = turn_PID.Target;
    s->actual = turn_PID.Actual;
    s->actual_previous = previous_actual;
    s->error = turn_PID.Error0;
    s->error_int = turn_PID.ErrorInt;
    s->kp = turn_PID.Kp;
    s->ki = turn_PID.Ki;
    s->kd = turn_PID.Kd;
    s->p = turn_PID.Kp * turn_PID.Error0;
    s->i = turn_PID.Ki * turn_PID.ErrorInt;
    s->d = -turn_PID.Kd * (turn_PID.Actual - previous_actual);
    s->unclamped = s->p + s->i + s->d;
    s->out = turn_PID.Out;
    s->left_pwm = LeftPWM;
    s->right_pwm = RightPWM;
    s->left_compare = (uint16_t)PWM_0_INST->COUNTERREGS.CC_01[1];
    s->right_compare = (uint16_t)PWM_0_INST->COUNTERREGS.CC_01[0];
    s->task = TASK;
    s->pos = car_pos;
    s->tracking = TrackFlag;
    s->entry = LineTracking_GetEntryDirection();
    s->checksum_errors = gyro->checksum_errors;
    s->frame_checksums = gyro->frame_checksums;
    curve_debug.sample_count = n + 1U;
}

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
    bd_exit_pending = 0U;
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
    LineTracking_SetEntryDirection(0);
    TB6612_Motor_Stop();
    if (curve_debug.active != 0U)
    {
        curve_event(CURVE_STOP);
        curve_debug.active = 0U;
    }
}

/* 触线后立即按路线转入，定时循迹继续完成捕线，不阻塞任务循环。 */
static void begin_diagonal_entry(int8_t direction)
{
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
    LineTracking_SetEntryDirection(direction);
    LeftPWM = (direction < 0) ? 0.0f : 2.0f * TASK_BASE_PWM;
    RightPWM = (direction < 0) ? 2.0f * TASK_BASE_PWM : 0.0f;
    Motor_SetPWM(1U, (int32_t)LeftPWM);
    Motor_SetPWM(2U, (int32_t)RightPWM);
    TrackFlag = 1U;
    if (direction > 0) curve_event(CURVE_D_ENTRY);
}

/*航向角保持*/
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

static void update_heading_hold(uint8_t immediate);

static void begin_bd_exit_pause(void)
{
    if (curve_debug.active != 0U)
    {
        const CurveGyroSample gyro = curve_read_gyro();
        curve_debug.exit_tick = Tick;
        curve_debug.exit_z = gyro.z;
        curve_debug.exit_gyro = gyro.rate;
    }
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
    curve_event(CURVE_B_PAUSE);
}

static uint8_t service_bd_exit_pause(void)
{
    if (bd_exit_pending == 0U) return 0U;
    if ((uint32_t)(Tick - bd_exit_tick) < TASK3_B_EXIT_PAUSE_MS) return 1U;

    bd_exit_pending = 0U;
    /* B停顿50ms后按A基准设置固定目标；当前Z只作反馈和诊断。
     * 图示顺时针为正，现有Z/航向环以左转为正，因此减去图示偏移。
     */
    const CurveGyroSample gyro = curve_read_gyro();
    const float target = wrap_heading(task34_a_heading - OPEN_LOOP2_AMEND);
    if (curve_debug.active != 0U)
    {
        curve_debug.lock_tick = Tick;
        curve_debug.lock_z = gyro.z;
        curve_debug.amend = OPEN_LOOP2_AMEND;
        curve_debug.locked_target = target;
        curve_debug.lock_count++;
    }
    if (begin_heading_hold(target, TASK_BASE_PWM) == 0U)
        stop_task();
    else
    {
        task34_b_aligning = 1U;
        curve_event(CURVE_B_ALIGN);
        update_heading_hold(1U);
    }
    return 1U;
}

/* 每10 ms闭环更新航向环 */
static void update_heading_hold(uint8_t immediate)
{
    const uint32_t now = Tick;
    uint8_t drive_now = 0U;

    if ((task34_b_settling != 0U) &&
        ((uint32_t)(now - bd_aligned_tick) >= TASK34_B_SETTLE_MS))
    {
        task34_b_settling = 0U;
        drive_now = 1U;
        immediate = 1U; /* 25ms到时接管，不再等下一次10ms节拍。 */
    }

    if ((immediate == 0U) &&
        ((uint32_t)(now - last_pd_tick) < HEADING_PD_PERIOD_MS)) return;
    last_pd_tick = now;

    const CurveGyroSample gyro = curve_read_gyro();
    const float previous_actual = turn_PID.Actual1;
    turn_PID.Actual = turn_PID.Target + wrap_heading(gyro.z - turn_PID.Target);
    PID_Update(&turn_PID);

    uint8_t aligned_now = 0U;
    if ((task34_b_aligning != 0U) &&
        (turn_PID.Error0 >= -TASK34_B_ALIGN_ERROR_DEG) &&
        (turn_PID.Error0 <= TASK34_B_ALIGN_ERROR_DEG))
    {
        task34_b_aligning = 0U;
        task34_b_settling = 1U;
        bd_aligned_tick = now;
        aligned_now = 1U;
    }

    if (task34_b_settling != 0U)
    {
        LeftPWM = 0.0f;
        RightPWM = 0.0f;
    }
    else if (task34_b_aligning != 0U)
    {
        /* B点先对准：内轮停，外轮按环输出转；两侧均不反转。 */
        LeftPWM  = (turn_PID.Out < 0.0f) ? -2.0f * turn_PID.Out : 0.0f;
        RightPWM = (turn_PID.Out > 0.0f) ?  2.0f * turn_PID.Out : 0.0f;
    }
    else
    {
        /* 正输出：左侧减、右侧加；电机1/2分别对应左/右。 */
        LeftPWM  = TASK_BASE_PWM - turn_PID.Out;
        RightPWM = TASK_BASE_PWM + turn_PID.Out;
    }

    Motor_SetPWM(1U, (int32_t) LeftPWM);
    Motor_SetPWM(2U, (int32_t)RightPWM);
    if (task34_b_settling != 0U) TB6612_Motor_Stop();
    if (aligned_now != 0U) curve_event(CURVE_B_SETTLE);
    if (drive_now != 0U) curve_event(CURVE_B_HEADING);
    curve_record_heading(&gyro, previous_actual);
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

/* 从A点启动时锁定当前航向。 */
static void start_task(uint8_t task)
{
    if (trace_state != OPEN)
    {
        stop_task();
        return;
    }

    bd_exit_pending = 0U;
    task34_b_aligning = 0U;
    task34_b_settling = 0U;
    running_task = task;
    car_pos = CAR_POS_A;
    previous_state = OPEN;
    TrackFlag = 0U;
    LineTracking_SetEntryDirection(0);
    if ((task == 3U) || (task == 4U)) curve_begin(task);
    turn_PID.OutMax =  PD_LIMIT;
    turn_PID.OutMin = -PD_LIMIT;
    const float start_heading = Gyro_Data.angle_z;
    float heading_target = start_heading;
    if ((task == 3U) || (task == 4U))
    {
        task34_a_heading = start_heading;
        heading_target = wrap_heading(task34_a_heading - OPEN_LOOP2_AC_ANGLE);
    }
    if (task == 4U)
    {
        task4_laps = 0U;
        task4_ac_heading = heading_target;
    }
    if (begin_heading_hold(heading_target,
                           ((task == 3U) || (task == 4U)) ? TASK_BASE_PWM : PD_LIMIT) == 0U)
    {
        stop_task();
        return;
    }
    update_heading_hold(1U);
    if ((task == 3U) || (task == 4U)) curve_event(CURVE_A_HEADING);
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
    else if (trace_state == OPEN)
    {
        update_heading_hold(0U);
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
                wrap_heading(turn_PID.Target + 180.0f + OPEN_LOOP1_AMEND);
            car_pos = CAR_POS_C;
            TrackFlag = 0U;
            Buzzer_for_a_while();
            if (begin_heading_hold(cd_heading, TASK_BASE_PWM) == 0U)
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
            if (curve_debug.active != 0U)
            {
                curve_debug.entry_tick = Tick;
                curve_debug.entry_z = Gyro_Data.angle_z;
                curve_event(CURVE_C_LINE);
            }
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
        if (curve_debug.phase == CURVE_D_ENTRY) curve_event(CURVE_D_LINE);
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
                    if (begin_heading_hold(task4_ac_heading, TASK_BASE_PWM) == 0U)
                        stop_task();
                    else
                    {
                        curve_event(CURVE_A_HEADING);
                        update_heading_hold(1U);
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
    const uint8_t requested_task = TASK;
    trace_state = read_trace_state();
    trace_status = (trace_state == CLOSE) ? 1U : 0U;

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
