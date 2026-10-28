#ifndef __JY901S_H
#define __JY901S_H

#include <stdint.h>
#include "ti_msp_dl_config.h"

typedef struct
{
    float angle_x;
    float angle_y;
    float angle_z;

    float gyro_x;
    float gyro_y;
    float gyro_z;

    float accel_x;
    float accel_y;
    float accel_z;

    uint8_t status;
} Gyro_Data_t;

extern volatile Gyro_Data_t Gyro_Data;
extern volatile uint8_t Gyro_UpdateFlag;

/* 只读诊断。约定（消费方依赖，勿破坏）：
 *   angle_tick / angle_seq / gyro_tick / gyro_seq 只由校验通过的帧推进，
 *   所以 (Tick - angle_tick) 可直接作为角度数据的新鲜度判据；
 *   checksum_errors 统计被整帧丢弃的坏帧。
 *   不再单列"最近一帧校验是否通过"这类字段：坏帧整帧丢弃之后它恒为 1，
 *   没有信息量，且是本次清理要删掉的冗余项。 */
typedef struct
{
    uint32_t angle_seq;
    uint32_t angle_tick;
    uint32_t gyro_seq;
    uint32_t gyro_tick;
    uint32_t checksum_errors;
} Gyro_Diag_t;
extern volatile Gyro_Diag_t Gyro_Diag;

void Gyro_Init(void);
void Gyro_ProcessByte(uint8_t byte);
void Gyro_ParseData(uint8_t *data, uint16_t length);
float wrap_heading(float angle);


#endif
