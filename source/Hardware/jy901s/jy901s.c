#include "JY901S.h"
#include <string.h>
#include "Hardware/Board/board.h"

volatile Gyro_Data_t Gyro_Data;
volatile uint8_t Gyro_UpdateFlag = 0;
volatile Gyro_Diag_t Gyro_Diag;

static uint8_t Gyro_RxBuffer[11];
static uint8_t Gyro_RxIndex = 0;

void Gyro_Init(void)
{
    memset((void *)&Gyro_Data, 0, sizeof(Gyro_Data_t));
    memset((void *)&Gyro_Diag, 0, sizeof(Gyro_Diag_t));
    Gyro_UpdateFlag = 0;
    Gyro_RxIndex = 0;

    DL_UART_Main_enableInterrupt(UART_3_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_EnableIRQ(UART_3_INST_INT_IRQN);
}

void Gyro_ProcessByte(uint8_t byte)
{
    if ((Gyro_RxIndex == 0) && (byte != 0x55))
    {
        return; 
    }

    Gyro_RxBuffer[Gyro_RxIndex++] = byte;

    if (Gyro_RxIndex >= 11)
    {
        Gyro_ParseData(Gyro_RxBuffer, 11);
        Gyro_RxIndex = 0;
    }
}

void Gyro_ParseData(uint8_t *data, uint16_t length)
{
    if ((length != 11) || (data[0] != 0x55))
    {
        return;
    }

    uint8_t sum = 0U;
    for (uint8_t i = 0U; i < 10U; i++) sum = (uint8_t)(sum + data[i]);

    /* 坏帧：整帧丢弃，不写 Gyro_Data、不置 Gyro_UpdateFlag。
     * 关键是也不推进角度时间戳 —— 否则后面的新鲜度判据会被坏帧反复"续期"，
     * 让一个已经被污染的值看起来永远是新鲜的。 */
    if (sum != data[10])
    {
        Gyro_Diag.checksum_errors++;
        return;
    }

    switch (data[1])
    {
        case 0x53:
            Gyro_Data.angle_x = (float)((int16_t)((data[3] << 8) | data[2])) / 32768.0f * 180.0f;
            Gyro_Data.angle_y = (float)((int16_t)((data[5] << 8) | data[4])) / 32768.0f * 180.0f;
            Gyro_Data.angle_z = (float)((int16_t)((data[7] << 8) | data[6])) / 32768.0f * 180.0f;
            Gyro_UpdateFlag = 1;
            Gyro_Diag.angle_tick = Tick;
            Gyro_Diag.angle_seq++;
            break;

        case 0x52:
            Gyro_Data.gyro_x = (float)((int16_t)((data[3] << 8) | data[2])) / 32768.0f * 2000.0f;
            Gyro_Data.gyro_y = (float)((int16_t)((data[5] << 8) | data[4])) / 32768.0f * 2000.0f;
            Gyro_Data.gyro_z = (float)((int16_t)((data[7] << 8) | data[6])) / 32768.0f * 2000.0f;
            Gyro_Diag.gyro_tick = Tick;
            Gyro_Diag.gyro_seq++;
            break;

        case 0x51:
            Gyro_Data.accel_x = (float)((int16_t)((data[3] << 8) | data[2])) / 32768.0f * 16.0f;
            Gyro_Data.accel_y = (float)((int16_t)((data[5] << 8) | data[4])) / 32768.0f * 16.0f;
            Gyro_Data.accel_z = (float)((int16_t)((data[7] << 8) | data[6])) / 32768.0f * 16.0f;
            break;

        default:
            break;
    }
}


/*��ת,�����ǰ�װ����*/
float wrap_heading(float angle)
{
    if (angle > 180.0f) angle -= 360.0f;
    if (angle < -180.0f) angle += 360.0f;
    return angle;
}

void UART3_IRQHandler(void)
{
    switch (DL_UART_Main_getPendingInterrupt(UART_3_INST))
    {

        case DL_UART_MAIN_IIDX_RX:
        {
            uint8_t byte = DL_UART_Main_receiveData(UART_3_INST);
            Gyro_ProcessByte(byte);
            break;
        }

        default:
            break;
    }
}
