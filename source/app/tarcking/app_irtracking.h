#ifndef _APP_IRTRACKING_H_
#define _APP_IRTRACKING_H_


#include "Hardware/Board/board.h"
#include <stdio.h>
#include <stdint.h>
#include "Hardware/LCD/lcd.h"

/* 运行/循迹使能标志：保留给旧循迹模式。 */
extern volatile uint8_t TrackFlag;    /* 循迹使能标志 */
extern volatile uint8_t trace_status;

void LineTracking_ShowRawOnLCD(void);
void LineWalking(void);
float LineTracking_GetError(void);
/* -1向左捕线，+1向右捕线，0恢复普通循迹；不包含等待。 */
void LineTracking_SetEntryDirection(int8_t direction);
int8_t LineTracking_GetEntryDirection(void);

#endif

