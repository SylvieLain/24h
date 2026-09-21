#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H
/*严禁删注释,有错可以修改但是得提醒我*/
#include <stdbool.h>
#include <stdint.h>
/*要求快速响应不需要回包检测 防止堵塞等麻烦事情 其次查询最快并且稳定的速度是多少 如何配置
计算好控制时间完成时间等整一套的控制频率因为要对齐pid 视觉等
*/
//丝杆长度100mm,12导程；每度位移=12/360=0.03333mm，命令精度由驱动器每转脉冲数决定
#define LIFT_SCREW_LEAD_MM              12.0f
#define LIFT_MOTOR_PULSES_PER_REV       3200U

#define LIFT_MOTOR_ADDR                 0x01U

/* Emm_V5硬件规格为3000RPM+；比赛机构在实测前先限制到1000RPM。

   硬件配置参数
*/
#define LIFT_MOTOR_DEFAULT_RPM          300U
#define LIFT_MOTOR_MAX_RPM             1000U
#define LIFT_MOTOR_DEFAULT_ACC          100U
#define LIFT_MOTOR_STATUS_MAX_REQUESTS  20U

/* 已通过左键把机械平衡位置写入驱动器；MCU重启后沿用该零点有效标志。 */
#define LIFT_MOTOR_USE_STORED_BALANCE    1U

/* 抬升方向判断标志位 */
#define LIFT_MOTOR_DIRECTION_INVERT      0U

//判断位
extern volatile bool if_return_balance;


typedef struct
{
    int32_t command_pulse;          /* MCU累计的绝对目标脉冲 */
    int32_t position_count;         /* S_CPOS回传值，65536 count/电机轴一圈 */
    uint8_t initialized;            /* [Brain补] LiftMotor_Init完成标志，上层启动前必须检查 */
    uint8_t balance_valid;          /* [Brain补] 零点是否有效（上电沿用驱动器存储零点 或 本次RecordBalanceHere标定） */
    uint16_t flags;                 /* [Brain补] S_FLAG回传位图：bit0=使能 bit1=到位（调试/防堵转用） */
} LiftMotorState;

/* 只调用一次：开启UART接收并使能单个Emm_V5电机，不自动运动或写零点。 */
void LiftMotor_Init(void);
//调试用,读取运动速度 rpm 是否初始化 转向 地址等参数设置和状态等方便ai和我调试,你来完善一下
void LiftMotor_GetState(LiftMotorState *state);

void LiftMotor_Enable(bool enable);
void LiftMotor_SetProfile(uint16_t rpm, uint8_t acc);

/* 本层只使用原始脉冲；丝杆导程和抬升高度换算放在后续上层模块。 */
bool LiftMotor_MoveRelativePulse(int32_t delta_pulse);
/* 绝对位置必须先调用LiftMotor_RecordBalanceHere建立本次上电的零点。 */
bool LiftMotor_MoveAbsolutePulse(int32_t target_pulse);
/* [Brain补] 跳过initialized/零点有效性前置检查的直接绝对运动——
   PD闭环每20ms都要下发绝对脉冲，不能每拍都做状态检查；仅限控制环内部使用。 */
bool LiftMotor_MoveAbsolutePulseDirect(int32_t target_pulse);

//换算内容和抬升函数,输入抬升高度->换算函数和抬升执行
int32_t Height_Trans(float height);
bool LiftMotor_Ctrl(float height);

/* [Brain补] 以下为第一版PID闭环/按键任务必需的接口（原头文件声明缺失，提醒）：
   - RecordBalanceHere 与 LIFT_MOTOR_USE_STORED_BALANCE 的关系：
     USE_STORED_BALANCE=1 时上电直接沿用驱动器零点(balance_valid=1)；
     RecordBalanceHere 供运行中按中键重新标定（Emm_V5_Reset_CurPos_To_Zero）。
   - RequestState 由主循环周期调用，内部按50ms节流交替查询S_CPOS/S_FLAG，
     运动指令后自动静默50ms防止响应错位（fire-and-forget的配套回读通道）。 */
void LiftMotor_Stop(void);
bool LiftMotor_RecordBalanceHere(void);
bool LiftMotor_ReturnToBalance(void);
void LiftMotor_Tick10ms(void);
void LiftMotor_Task(void);
void LiftMotor_RequestState(void);


#endif
