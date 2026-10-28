#ifndef _APP_HEADING_H_
#define _APP_HEADING_H_

#include "middle/mid_pid.h"
#include <stdint.h>

/* 航向 PID 参数*/
extern PID_t turn_PID;

void Heading_Init(void);
/* 目标及阶段准备完成后发布任务；传0停用航向控制。 */
void Heading_SetTask(uint8_t task);
/* immediate=0仅供10 ms中断；=1供主循环在启动/阶段切换时立即更新。 */
void update_heading(uint8_t immediate);

#endif
