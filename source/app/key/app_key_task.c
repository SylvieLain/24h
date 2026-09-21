/**
 * @file    app_key_task.c
 * @brief   按键回调层实现。
 *
 * 回调由 middle/mid_button.c 的 flex_button_scan() 在检测到事件时触发，
 * 本文件只根据事件做轻量置位/清零，不做电机、LCD 或阻塞操作。
 *
 */

#include "app/key/app_key_task.h"
#include "app/tarcking/app_irtracking.h"
#include "Hardware/Buzzer&Light/B&L.h"

//上左中右

/* 当前按键处理模式，默认 0；供后续模式分发使用 */
static char app_key_current_mode = 0;

/**
 * @brief  设置当前按键处理模式
 */
void set_app_key_current_mode(char mode)
{
    app_key_current_mode = mode;
}

/**
 * @brief  获取当前按键处理模式
 */
char get_app_key_current_mode(void)
{
    return app_key_current_mode;
}

/**
 * @brief  中键回调：当前任务1不使用
 */
void btn_mid_cb(flex_button_t *btn)
{
    if (btn == NULL)
    {
        return;
    }

    (void)btn;
}

/**
 * @brief  上键回调：单击切换任务1
 */
void btn_up_cb(flex_button_t *btn)
{
    if (btn == NULL)
    {
        return;
    }

    if ((flex_button_event_t)btn->event == FLEX_BTN_PRESS_CLICK)
    {
        Task1_Toggle();
    }
}



/**
 * @brief  左键回调：暂不响应（保留接口）
 */
void btn_left_cb(flex_button_t *btn)
{
    if (btn == NULL)
    {
        return;
    }

    (void)btn;
}

/**
 * @brief  右键回调：暂不响应（保留接口）
 */
void btn_right_cb(flex_button_t *btn)
{
    (void)btn;
}
