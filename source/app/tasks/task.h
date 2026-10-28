#ifndef _APP_TASK_H_
#define _APP_TASK_H_

#include <stdint.h>

/*任务调度与定义*/
#define TASK_BASE_PWM              3500.0f
#define HEADING_PD_PERIOD_MS       10U
#define TASK34_B_ALIGN_ERROR_DEG   0.5f
#define TASK34_B_SETTLE_MS         25U
#define TASK3_B_EXIT_PAUSE_MS      50U

/*12循迹环*/
#define LINE_KP     1200.0f
#define LINE_KD      700.0f  /* 10 ms固定周期：使用相邻误差差值。 */
#define PD_LIMIT    9999.0f
/*任务3循迹环*/
#define TASK34_LINE_KP  1200.0f
#define TASK34_LINE_KD  500.0f
#define PD34_LIMIT      9999.0f
/* 任务3/4：
   A点直接对准c放车，启动航向并即AC基准；顺时针偏移为正。 */
#define OPEN_LOOP2_AMEND           (102.68f)

typedef enum
{
    OPEN = 0,  /* 未见黑线：任务1/2直线、任务3对角段使用 Z 角 PD。 */
    CLOSE = 1  /* 至少一路见黑：用于识别入线/出线边沿。 */
} TraceState;

enum
{
    CAR_POS_A = 0,
    CAR_POS_B = 1,
    CAR_POS_C = 2,
    CAR_POS_D = 3
};


extern volatile uint8_t		TASK;				/* TASK 由按键提交 */
extern volatile uint8_t		car_pos;
extern volatile TraceState	trace_state;
extern volatile float		task_speed;
extern volatile float		task34_a_heading;
extern volatile uint8_t		task34_b_aligning;
extern volatile uint8_t		task34_b_settling;
extern volatile uint32_t		last_pd_tick;
extern volatile uint32_t		bd_aligned_tick;

/* 接口 */
void task_loop(void);
void open_loop1(void);
void open_loop2(void);

#endif
