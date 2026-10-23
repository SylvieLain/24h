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

/* 只读诊断：计数为实际解析次数；校验仅记录，不改变现有接收行为。 */
typedef struct
{
    uint32_t angle_seq;
    uint32_t angle_tick;
    uint32_t gyro_seq;
    uint32_t gyro_tick;
    uint32_t checksum_errors;
    uint32_t angle_checksum_ok;
    uint32_t gyro_checksum_ok;
} Gyro_Diag_t;
extern volatile Gyro_Diag_t Gyro_Diag;

void Gyro_Init(void);
void Gyro_ProcessByte(uint8_t byte);
void Gyro_ParseData(uint8_t *data, uint16_t length);

#endif
