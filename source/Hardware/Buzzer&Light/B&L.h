#ifndef __B_L_H
#define __B_L_H

#include <stdint.h>

void BL_Init(void);

void Buzzer_ON(void);
void Buzzer_OFF(void);
void Buzzer_Toggle(void);
void Buzzer_Beep(uint16_t duration_ms);
void Buzzer_IRQService(void);
void Buzzer_for_a_while(void);

void BL_ON(void);
void BL_OFF(void);
void BL_Toggle(void);

#endif
