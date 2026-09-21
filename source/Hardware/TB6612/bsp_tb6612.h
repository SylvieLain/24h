#ifndef _BSP_TB6612_H
#define _BSP_TB6612_H

#include "Hardware/Board/board.h"

#define AIN1_OUT(X)  ( (X) ? DL_GPIO_setPins(TB6612_AIN1_PORT, TB6612_AIN1_PIN) : DL_GPIO_clearPins(TB6612_AIN1_PORT, TB6612_AIN1_PIN))
#define AIN2_OUT(X)  ( (X) ? DL_GPIO_setPins(TB6612_AIN2_PORT, TB6612_AIN2_PIN) : DL_GPIO_clearPins(TB6612_AIN2_PORT, TB6612_AIN2_PIN))

#define BIN1_OUT(X)  ( (X) ? DL_GPIO_setPins(TB6612_BIN1_PORT, TB6612_BIN1_PIN) : DL_GPIO_clearPins(TB6612_BIN1_PORT, TB6612_BIN1_PIN))
#define BIN2_OUT(X)  ( (X) ? DL_GPIO_setPins(TB6612_BIN2_PORT, TB6612_BIN2_PIN) : DL_GPIO_clearPins(TB6612_BIN2_PORT, TB6612_BIN2_PIN))

void TB6612_Motor_Stop(void);
void AO_Control(uint8_t dir, uint32_t speed);
void BO_Control(uint8_t dir, uint32_t speed);

void Motor_SetPWM(uint8_t motor, int32_t pwm);

/* 电机输出中间量（原 app_ui 迁入） */
extern volatile float DeadZone;   /* 死区补偿量，默认 0.0f（暂无依据不启用补偿） */
extern volatile float AvePWM;     /* 平均 PWM 输出 */
extern volatile float DifPWM;     /* 差速 PWM 输出 */
extern volatile float LeftPWM;    /* 左轮 PWM 输出 */
extern volatile float RightPWM;   /* 右轮 PWM 输出 */

#endif  /* _BSP_TB6612_H */
