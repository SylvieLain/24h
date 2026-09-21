#include <ti_msp_dl_config.h>
#include "B&L.h"

static volatile uint16_t buzzer_ticks_10ms = 0U;

void BL_Init(void)
{
    buzzer_ticks_10ms = 0U;
    DL_GPIO_enableOutput(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
    DL_GPIO_clearPins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN);
}

void Buzzer_ON(void)     { DL_GPIO_setPins   (Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }
void Buzzer_OFF(void)    { DL_GPIO_clearPins (Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }
void Buzzer_Toggle(void) { DL_GPIO_togglePins(Buzzer_Light_PORT, Buzzer_Light_Buzzer_PIN); }

void Buzzer_Beep(uint16_t duration_ms)
{
    buzzer_ticks_10ms = (uint16_t)((duration_ms + 9U) / 10U);
    if (buzzer_ticks_10ms == 0U)
    {
        buzzer_ticks_10ms = 1U;
    }
    Buzzer_ON();
}

void Buzzer_Service_10ms(void)
{
    if (buzzer_ticks_10ms == 0U)
    {
        return;
    }
    buzzer_ticks_10ms--;
    if (buzzer_ticks_10ms == 0U)
    {
        Buzzer_OFF();
    }
}

void Buzzer_for_a_while(void)
{
    Buzzer_Beep(100U);
}

void BL_ON(void)     { Buzzer_ON();     }
void BL_OFF(void)    { Buzzer_OFF();    }
void BL_Toggle(void) { Buzzer_Toggle(); }
