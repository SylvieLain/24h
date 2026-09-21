#ifndef _APP_TASK_H_
#define _APP_TASK_H_

#include <stdint.h>

typedef enum
{
    OPEN = 0,
    CLOSE = 1
} TraceState;

extern volatile uint8_t TASK;
extern volatile TraceState trace_state;

void task_loop(void);
void open_loop1(void);
void open_loop2(void);

#endif
