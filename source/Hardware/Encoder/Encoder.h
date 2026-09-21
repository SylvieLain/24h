#ifndef _ENCODER_H_
#define _ENCODER_H_

#include "ti_msp_dl_config.h"
#include "Hardware/Board/board.h"

typedef struct{
    int Should_Get_Encoder_Count;   // 将要获得的编码器计数
    int Obtained_Get_Encoder_Count; // 得到的编码器的计数
    int Total_Encoder_Count;        // 从清零开始累计的编码器总计数
}Encoder;

// 获得绝对值
#define ABS(a)      (a>0 ? a:(-a))

#define SPEED_ENCODER_VALUE_MAX ( -(SPEED_ENCODER_VALUE_MIN) )
#define SPEED_ENCODER_VALUE_MID 0
#define SPEED_ENCODER_VALUE_MIN (-100)

#define DISTANCE_ENCODER_VALUE_MAX ( -(DISTANCE_ENCODER_VALUE_MIN) )
#define DISTANCE_ENCODER_VALUE_MID 0
#define DISTANCE_ENCODER_VALUE_MIN (-360)


/* 编码器计数（去掉 static 以便调试/DAP 直接读取）
 * Encoder_A：左后轮(轮2)，E1: A=PA1 B=PA0
 * Encoder_B：右后轮(轮4)，E2: A=PA8 B=PA9
 */
extern volatile Encoder Encoder_A;
extern volatile Encoder Encoder_B;

/* 速度反馈与分频计数（原 app_ui 迁入） */
extern volatile uint8_t feedback_cnt;
extern volatile float AveSpeed;
extern volatile float LeftSpeed;
extern volatile float RightSpeed;

void Encoder_Init(void);
int Encoder_Get(int dir);
int Encoder_GetTotal(int dir);
void Encoder_ClearTotal(void);
int Encoder_GetAverageTotal(void);
void Encoder_UpdateSpeed(void);

#endif
