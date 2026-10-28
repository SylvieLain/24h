#ifndef _APP_IRTRACKING_H_
#define _APP_IRTRACKING_H_


#include "Hardware/Board/board.h"
#include "app/tasks/task.h"
#include <stdio.h>
#include <stdint.h>
#include "Hardware/LCD/lcd.h"

/* 运行/循迹使能标志：保留给旧循迹模式。 */
extern volatile uint8_t TrackFlag;    /* 循迹使能标志 */

void LineWalking(void);
float LineTracking_GetError(void);
void LineTracking_ShowRawOnLCD(void);

/* 每 10 ms 与 LineWalking() 同周期调用一次，返回已限幅的差速量。 */
float LineTracking_ComputeDuty(float base_speed, uint8_t task);
/* 停车 / 离开循迹段时调用，清 D 项状态，避免下一拍带旧误差冲击。 */
void LineTracking_ResetPd(void);

/* -1向左捕线，+1向右捕线，0恢复普通循迹；不包含等待。 */
void LineTracking_SetEntryDirection(int8_t direction);
int8_t LineTracking_GetEntryDirection(void);
TraceState read_trace_state(void);


#endif

