/* Emm_V5 丝杆抬升电机驱动（UART_2, 非阻塞fire-and-forget）
 *
 * 移植自第一版验证过的实现，按本工程 stepper_motor.h 的精简接口适配：
 *  - 运动指令只发不收，不等回包（.h伪代码要求：快速响应防堵塞）
 *  - 状态回读走后台：LiftMotor_RequestState 50ms节流交替查 S_CPOS/S_FLAG，
 *    UART2 RX 中断状态机解析回包（addr+cmd+data+0x6B）
 *  - 查询节流 guard：运动指令后静默50ms、清零后300ms，防止梯形规划期间
 *    位置回读错位
 */
#include "Hardware/Motor/42Motor.h"
#include "Hardware/Motor/stepper_motor.h"
#include "Hardware/Buzzer&Light/B&L.h"
#include "ti_msp_dl_config.h"

#include <limits.h>

volatile bool if_return_balance = false;

typedef enum
{
    RX_WAIT_ADDR = 0,
    RX_WAIT_CMD,
    RX_COLLECT,
} LiftRxState;

typedef struct
{
    LiftRxState state;
    uint8_t buf[8];
    uint8_t index;
    uint8_t expected_length;
} LiftRxContext;

static volatile LiftMotorState g_lift_state = {0};
static LiftRxContext g_rx = {RX_WAIT_ADDR, {0}, 0, 0};
static uint16_t g_motion_rpm = LIFT_MOTOR_DEFAULT_RPM;
static uint8_t g_motion_acc = LIFT_MOTOR_DEFAULT_ACC;
static uint8_t g_next_query_is_flags = 0U;
static volatile uint32_t g_lift_tick_10ms = 0U;
static volatile uint32_t g_query_resume_tick = 0U;

/* S_FLAG bit1 = 到位标志（Emm_V5手册） */
#define LIFT_FLAG_ARRIVED         0x02U
/* 查询静默期：单位10ms tick。运动后50ms、清零标定后300ms */
#define LIFT_QUERY_GUARD_ZERO_TICKS   30U
#define LIFT_QUERY_GUARD_MOTION_TICKS 5U

static void lift_suspend_queries(uint32_t ticks)
{
    g_query_resume_tick = g_lift_tick_10ms + ticks;
}

static uint32_t pulse_magnitude(int32_t pulse)
{
    if (pulse >= 0)
    {
        return (uint32_t)pulse;
    }

    return (uint32_t)(-(int64_t)pulse);
}

static uint8_t pulse_direction(int32_t pulse)
{
    uint8_t direction = (pulse >= 0) ? 0U : 1U;

#if LIFT_MOTOR_DIRECTION_INVERT
    direction ^= 1U;
#endif

    return direction;
}

static uint32_t bytes_be_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint8_t expected_packet_length(uint8_t command)
{
    switch (command)
    {
        case 0x36:
            return 8U; /* S_CPOS: addr+cmd+sign+4B+0x6B */

        case 0x3A:
            return 4U; /* S_FLAG: addr+cmd+flags+0x6B */

        case 0x0A: /* 当前位置清零应答 */
        case 0xF3: /* 电机使能应答 */
        case 0xFD: /* 位置模式控制应答 */
            return 4U; /* addr+cmd+status+0x6B */

        default:
            return 0U;
    }
}

static void dispatch_packet(const uint8_t *packet)
{
    if (packet[1] == 0x36)
    {
        uint32_t magnitude = bytes_be_u32(&packet[3]);
        if (magnitude <= (uint32_t)INT32_MAX)
        {
            int32_t signed_position = (int32_t)magnitude;
            g_lift_state.position_count = packet[2] ? -signed_position : signed_position;
        }
    }
    else if (packet[1] == 0x3A)
    {
        g_lift_state.flags = packet[2];
    }
}

void LiftMotor_Init(void)
{
    if (g_lift_state.initialized != 0U)
    {
        return;
    }

    /* 清空 RX FIFO 残留数据，防止上电瞬间的噪声字节卡住状态机 */
    while (!DL_UART_Main_isRXFIFOEmpty(UART_2_INST))
    {
        (void)DL_UART_Main_receiveData(UART_2_INST);
    }
    DL_UART_Main_clearInterruptStatus(UART_2_INST, DL_UART_MAIN_INTERRUPT_RX);
    DL_UART_Main_enableInterrupt(UART_2_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(UART_2_INST_INT_IRQN);
    /* 电机回包抢占其他应用中断（第一版验证过的优先级安排） */
    NVIC_SetPriority(UART_2_INST_INT_IRQN, 0U);
    NVIC_EnableIRQ(UART_2_INST_INT_IRQN);

    g_lift_state.initialized = 1U;
    /* 左键已把平衡位置写入驱动器零点，重启沿用（.h: USE_STORED_BALANCE） */
    g_lift_state.balance_valid = LIFT_MOTOR_USE_STORED_BALANCE;
    g_lift_state.command_pulse = 0;
    /* 上电立即使能：漏掉状态回包不会永久卡死位置指令 */
    Emm_V5_En_Control(LIFT_MOTOR_ADDR, true, false, UART_2_INST);
    lift_suspend_queries(LIFT_QUERY_GUARD_ZERO_TICKS);
    /* [Brain审定] 原 Light_ON() 在本工程无任何定义，曾导致编译阻塞。
       本工程 B&L 模块只暴露蜂鸣器接口（BL_ON() == Buzzer_ON()），并无独立灯接口；
       此处暂以 BL_ON() 代替仅为消除链接/编译阻塞，属临时替代：原意“点亮”无法实现，
       正式声光提示接口待确认。注：本文件当前仅经 mid_ball_link 路径调用，该路径已移出构建，
       故本处运行时暂不执行。 */
    BL_ON();
}

void LiftMotor_Enable(bool enable)
{
    if (g_lift_state.initialized == 0U)
    {
        return;
    }

    Emm_V5_En_Control(LIFT_MOTOR_ADDR, enable, false, UART_2_INST);
}

void LiftMotor_SetProfile(uint16_t rpm, uint8_t acc)
{
    if (rpm == 0U)
    {
        rpm = 1U;
    }
    else if (rpm > LIFT_MOTOR_MAX_RPM)
    {
        rpm = LIFT_MOTOR_MAX_RPM;
    }

    g_motion_rpm = rpm;
    g_motion_acc = acc;
}

int32_t Height_Trans(float height)
{
    /* 每mm脉冲数 = 3200脉冲/转 ÷ 12mm/转 ≈ 266.67 脉冲/mm */
    float pulse = height * (float)LIFT_MOTOR_PULSES_PER_REV / LIFT_SCREW_LEAD_MM;

    if (pulse >= 0.0f)
    {
        return (int32_t)(pulse + 0.5f);
    }

    return (int32_t)(pulse - 0.5f);
}

bool LiftMotor_Ctrl(float height)
{
    return LiftMotor_MoveRelativePulse(Height_Trans(height));
}

bool LiftMotor_MoveRelativePulse(int32_t delta_pulse)
{
    int64_t next_command;

    if ((g_lift_state.initialized == 0U) ||
        (delta_pulse == 0))
    {
        return false;
    }

    next_command = (int64_t)g_lift_state.command_pulse + delta_pulse;
    if ((next_command > INT32_MAX) || (next_command < INT32_MIN))
    {
        return false;
    }

    Emm_V5_Pos_Control(LIFT_MOTOR_ADDR,
                       pulse_direction(delta_pulse),
                       g_motion_rpm,
                       g_motion_acc,
                       pulse_magnitude(delta_pulse),
                       false,
                       false,
                       UART_2_INST);
    lift_suspend_queries(LIFT_QUERY_GUARD_MOTION_TICKS);
    g_lift_state.command_pulse = (int32_t)next_command;
    return true;
}

bool LiftMotor_MoveAbsolutePulse(int32_t target_pulse)
{
    int32_t return_delta;

    if ((g_lift_state.initialized == 0U) ||
        (g_lift_state.balance_valid == 0U))
    {
        return false;
    }

    /* EMM V5 忽略绝对位置命令中脉冲场为0的帧；
       回平衡点用等效相对运动实现（第一版验证过的规避） */
    if ((target_pulse == 0) && (g_lift_state.command_pulse != 0))
    {
        return_delta = -g_lift_state.command_pulse;
        Emm_V5_Pos_Control(LIFT_MOTOR_ADDR,
                           pulse_direction(return_delta),
                           g_motion_rpm,
                           g_motion_acc,
                           pulse_magnitude(return_delta),
                           false,
                           false,
                           UART_2_INST);
        lift_suspend_queries(LIFT_QUERY_GUARD_MOTION_TICKS);
        g_lift_state.command_pulse = 0;
        return true;
    }

    Emm_V5_Pos_Control(LIFT_MOTOR_ADDR,
                       pulse_direction(target_pulse),
                       g_motion_rpm,
                       g_motion_acc,
                       pulse_magnitude(target_pulse),
                       true,
                       false,
                       UART_2_INST);
    lift_suspend_queries(LIFT_QUERY_GUARD_MOTION_TICKS);
    g_lift_state.command_pulse = target_pulse;
    return true;
}

bool LiftMotor_MoveAbsolutePulseDirect(int32_t target_pulse)
{
    int32_t return_delta;

    if (g_lift_state.initialized == 0U)
    {
        return false;
    }

    if ((target_pulse == 0) && (g_lift_state.command_pulse != 0))
    {
        return_delta = -g_lift_state.command_pulse;
        Emm_V5_Pos_Control(LIFT_MOTOR_ADDR,
                            pulse_direction(return_delta),
                            g_motion_rpm,
                            g_motion_acc,
                            pulse_magnitude(return_delta),
                            false,
                            false,
                            UART_2_INST);
        lift_suspend_queries(LIFT_QUERY_GUARD_MOTION_TICKS);
        g_lift_state.command_pulse = 0;
        return true;
    }

    Emm_V5_Pos_Control(LIFT_MOTOR_ADDR,
                        pulse_direction(target_pulse),
                        g_motion_rpm,
                        g_motion_acc,
                        pulse_magnitude(target_pulse),
                        true,
                        false,
                        UART_2_INST);
    lift_suspend_queries(LIFT_QUERY_GUARD_MOTION_TICKS);
    g_lift_state.command_pulse = target_pulse;
    return true;
}

void LiftMotor_Stop(void)
{
    if (g_lift_state.initialized != 0U)
    {
        Emm_V5_Stop_Now(LIFT_MOTOR_ADDR, false, UART_2_INST);
    }
}

bool LiftMotor_RecordBalanceHere(void)
{
    if (g_lift_state.initialized == 0U)
    {
        return false;
    }

    Emm_V5_Reset_CurPos_To_Zero(LIFT_MOTOR_ADDR, UART_2_INST);
    lift_suspend_queries(LIFT_QUERY_GUARD_ZERO_TICKS);
    g_lift_state.command_pulse = 0;
    g_lift_state.balance_valid = 1U;
    return true;
}

bool LiftMotor_ReturnToBalance(void)
{
    if (g_lift_state.balance_valid == 0U)
    {
        return false;
    }

    return LiftMotor_MoveAbsolutePulse(0);
}

void LiftMotor_Tick10ms(void)
{
    g_lift_tick_10ms++;
}

void LiftMotor_Task(void)
{
    /* 左键请求回平衡点（旧工程按键语义保留） */
    if (if_return_balance)
    {
        if_return_balance = false;
        (void)LiftMotor_ReturnToBalance();
    }
}

void LiftMotor_RequestState(void)
{
    if (g_lift_state.initialized == 0U)
    {
        return;
    }

    /* 查询节流：静默期内不发查询（.h伪代码"查询最快且稳定"的答案：
       50ms交替查询在本链路上稳定，运动期间自动暂停防错位） */
    if ((int32_t)(g_lift_tick_10ms - g_query_resume_tick) < 0)
    {
        return;
    }

    if (g_next_query_is_flags == 0U)
    {
        Emm_V5_Read_Sys_Params(LIFT_MOTOR_ADDR, S_CPOS, UART_2_INST);
        g_next_query_is_flags = 1U;
    }
    else
    {
        Emm_V5_Read_Sys_Params(LIFT_MOTOR_ADDR, S_FLAG, UART_2_INST);
        g_next_query_is_flags = 0U;
    }
}

void LiftMotor_GetState(LiftMotorState *state)
{
    if (state == NULL)
    {
        return;
    }

    state->command_pulse = g_lift_state.command_pulse;
    state->position_count = g_lift_state.position_count;
    state->initialized = g_lift_state.initialized;
    state->balance_valid = g_lift_state.balance_valid;
    state->flags = g_lift_state.flags;
}

void UART2_IRQHandler(void)
{
    switch (DL_UART_Main_getPendingInterrupt(UART_2_INST))
    {
        case DL_UART_MAIN_IIDX_RX:
            while (!DL_UART_Main_isRXFIFOEmpty(UART_2_INST))
            {
                uint8_t byte = DL_UART_Main_receiveData(UART_2_INST);

                switch (g_rx.state)
                {
                    case RX_WAIT_ADDR:
                        if (byte == LIFT_MOTOR_ADDR)
                        {
                            g_rx.buf[0] = byte;
                            g_rx.index = 1U;
                            g_rx.state = RX_WAIT_CMD;
                        }
                        break;

                    case RX_WAIT_CMD:
                        g_rx.buf[g_rx.index++] = byte;
                        g_rx.expected_length = expected_packet_length(byte);
                        if (g_rx.expected_length == 0U)
                        {
                            g_rx.state = RX_WAIT_ADDR;
                            g_rx.index = 0U;
                        }
                        else
                        {
                            g_rx.state = RX_COLLECT;
                        }
                        break;

                    case RX_COLLECT:
                        if (g_rx.index < (uint8_t)sizeof(g_rx.buf))
                        {
                            g_rx.buf[g_rx.index++] = byte;
                        }
                        else
                        {
                            g_rx.state = RX_WAIT_ADDR;
                            g_rx.index = 0U;
                            break;
                        }

                        if (g_rx.index >= g_rx.expected_length)
                        {
                            if (g_rx.buf[g_rx.index - 1U] == 0x6B)
                            {
                                dispatch_packet(g_rx.buf);
                            }

                            g_rx.state = RX_WAIT_ADDR;
                            g_rx.index = 0U;
                        }
                        break;

                    default:
                        g_rx.state = RX_WAIT_ADDR;
                        g_rx.index = 0U;
                        break;
                }
            }
            break;

        default:
            break;
    }
}
