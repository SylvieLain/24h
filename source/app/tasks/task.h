#ifndef _APP_TASK_H_
#define _APP_TASK_H_

#include <stdint.h>


#define TASK_BASE_PWM              3500.0f
#define HEADING_PD_PERIOD_MS       10U
#define TASK34_B_ALIGN_ERROR_DEG   0.5f
#define TASK34_B_SETTLE_MS         25U

/*12循迹环*/
#define LINE_KP     1200.0f
#define LINE_KD      700.0f  /* 10 ms固定周期：使用相邻误差差值。 */
#define PD_LIMIT    9999.0f
/*任务3循迹环：保留已有参数名*/
#define TASK34_LINE_KP  1200.0f
#define TASK34_LINE_KD  500.0f
#define PD34_LIMIT      9999.0f

/*跑通再限幅*/
#define OPEN_LOOP1_AMEND           (-0.0f)
/* 任务3/4：A点已朝C放车，启动航向即AC基准；顺时针偏移为正。 */
#define OPEN_LOOP2_AC_ANGLE        ( 0.0f)
#define OPEN_LOOP2_AMEND           (102.68f)

typedef enum
{
    OPEN = 0,  /* 未见黑线：任务1/2直线、任务3对角段使用 Z 角 PD。 */
    CLOSE = 1  /* 至少一路见黑：用于识别入线/出线边沿。 */
} TraceState;

enum
{
    CAR_POS_A = 0,
    CAR_POS_B,
    CAR_POS_C,
    CAR_POS_D
};

extern volatile uint8_t TASK;
extern volatile uint8_t car_pos;
extern volatile TraceState trace_state;

/* 单次试跑保留到下次启动任务3/4；reset会清空，满后不覆盖旧样本。 */
#define CURVE_TRACE_SAMPLES 240U
#define CURVE_TRACE_EVENTS 32U
typedef enum
{
    CURVE_STOP = 0, CURVE_C_LINE = 1, CURVE_B_PAUSE = 2,
    CURVE_B_HEADING = 3, CURVE_D_ENTRY = 4, CURVE_D_LINE = 5,
    CURVE_A_HEADING = 6, CURVE_B_ALIGN = 7, CURVE_B_SETTLE = 8
} CurvePhase;

typedef struct
{
    uint32_t version, run_id, active, phase, source_task;
    uint32_t sample_count, event_count, dropped, lock_count;
    uint32_t entry_tick, exit_tick, lock_tick;
    float entry_z, exit_z, exit_gyro, lock_z, amend, locked_target;
    uint32_t last_heading_tick;
} CurveDebug;

typedef struct
{
    uint32_t tick, angle_seq, angle_tick, gyro_seq, gyro_tick;
    float raw_z, gyro_z, target, actual, actual_previous, error, error_int;
    float kp, ki, kd, p, i, d, unclamped, out, left_pwm, right_pwm;
    uint16_t left_compare, right_compare;
    uint8_t task, pos, tracking;
    int8_t entry;
    uint32_t checksum_errors, frame_checksums;
} CurveHeadingSample;

typedef struct
{
    uint32_t tick, phase;
    float raw_z, gyro_z, target, left_pwm, right_pwm;
    uint8_t task, pos, tracking;
    int8_t entry;
} CurveEvent;

extern volatile CurveDebug curve_debug;
extern volatile CurveHeadingSample curve_heading_trace[CURVE_TRACE_SAMPLES];
extern volatile CurveEvent curve_events[CURVE_TRACE_EVENTS];

void task_loop(void) ;
void open_loop1(void);
void open_loop2(void);

#endif
