
uint8_t TASK=0;
/*将小车放在位置 A 点，小车能自动行驶到 B 点停车，停车时有声光提示。用时不大于 15 秒*/
switch (TASK)
{
case 0:
    Track_Flag=0;
    TB6612_Motor_Stop();
    break;

case 1:


default:
    break;
}