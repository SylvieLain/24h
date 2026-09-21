/**
 * @file    app_key_task.h
 * @brief   按键任务模块头文件
 * @details 基于 FlexibleButton (middle/mid_button.h) 的五向按键回调层。
 *          - 由 middle/mid_button.c 中的 user_button_init() / user_button_init_again()
 *            将 5 个物理按键 (UP/LEFT/RIGHT/DOWN/MID) 的 flex_button_t 实例
 *            绑定到本文件的 btn_*_cb 回调；
 *          - 周期性调用 flex_button_scan() (建议 5~20 ms) 触发回调；
 *          - 通过 current_mode 在回调内部做模式分发，具体按键逻辑在 app_key_task.c 中实现。
 * @note    本文件仅声明接口，实现位于 app_key_task.c
 */

#ifndef _APP_KEY_TASK_H_
#define _APP_KEY_TASK_H_

#include "ti_msp_dl_config.h"   // TI MSPM0 SysConfig 生成的引脚/时钟配置
#include "middle/mid_button.h"  // FlexibleButton 数据结构与事件枚举 (flex_button_t, flex_button_event_t)

/* ==================== 模式管理 ==================== */

/**
 * @brief  设置当前按键处理模式
 * @param  mode 模式字，由应用层自定义 (如 0=菜单导航, 1=参数调节等)
 * @note   回调函数内部通过 get_app_key_current_mode() 判断 mode 再分发具体动作
 */
void set_app_key_current_mode(char mode);

/**
 * @brief  获取当前按键处理模式
 * @return 当前模式字 (与 set_app_key_current_mode 写入的值对应)
 */
char get_app_key_current_mode(void);

/* ==================== FlexibleButton 回调 ==================== */
/**
 * @brief  按键事件回调原型说明 (以下 5 个函数均为此原型)
 * @param  btn 触发事件的按键实例指针，可通过 btn->event 区分
 *             FLEX_BTN_PRESS_CLICK / DOUBLE_CLICK / SHORT_START / LONG_START 等
 *             (见 mid_button.h:flex_button_event_t)，通过 btn->click_cnt 获取连击次数
 * @note   回调由 flex_button_process() 在检测到对应事件时自动调用，
 *         无需应用层手动轮询；回调内应只做轻量操作，耗时工作置标志位由主任务处理
 */

void btn_up_cb(flex_button_t *btn);     /**< @brief 上键回调 */
void btn_left_cb(flex_button_t *btn);   /**< @brief 左键回调 */
void btn_right_cb(flex_button_t *btn);  /**< @brief 右键回调 */
void btn_mid_cb(flex_button_t *btn);    /**< @brief 中键/确认键回调 */

#endif /* _APP_KEY_TASK_H_ */
