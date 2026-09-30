#ifndef _APP_TASK_H_
#define _APP_TASK_H_

#include <stdint.h>


#define TASK_BASE_PWM              3500.0f
#define HEADING_PD_PERIOD_MS       10U

// #define LINE_KP     1200.0f
#define PD_LIMIT    9999.0f

/*跑通再限幅*/

#define OPEN_LOOP1_AMEND           (-14.0f)
#define OPEN_LOOP2_AMEND           (50.0f)

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

void task_loop(void);
void open_loop1(void);
void open_loop2(void);

#endif
