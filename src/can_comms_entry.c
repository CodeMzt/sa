/*
 * can_comms_entry.c
 *
 *   Created on: 2026年1月24日
 *       Author: Ma Ziteng
 *  Description: can通信任务，用于接收电机数据以及发送控制指令。
 */

#include "can_comms_entry.h"
#include "shared_data.h"
#include "sys_log.h"
#include "drv_canfd.h"
#include "robstride_motor.h"

/**
 * @brief  初始化所有关节电机（CSP位置模式）和夹爪电机（运控模式）
 *
 *  关节1~4 初始化流程（CSP位置模式）：
 *    1. 切换到 CSP 位置模式
 *    2. 使能电机
 *    3. 设置速度限制
 *
 *  夹爪 初始化流程（运控模式，上电默认即为运控模式）：
 *    1. 使能电机（无需切换模式）
 *
 *  @return FSP_SUCCESS 或首个错误码
 */
static fsp_err_t arm_motors_init(void) {
    fsp_err_t err;

    /* ---- 关节1~4：CSP 位置模式 ---- */
    const uint8_t joint_ids[4] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4,
    };

    for (uint8_t i = 0; i < 4U; i++) {
        uint8_t id = joint_ids[i];

        /* 1. 切换到 CSP 位置模式（掉电丢失，每次上电需重新设置） */
        err = robstride_set_run_mode(id, ROBSTRIDE_MODE_POSITION_CSP);
        if (err != FSP_SUCCESS) return err;
        vTaskDelay(5);

        /* 2. 使能电机 */
        err = robstride_enable(id);
        if (err != FSP_SUCCESS) return err;
        vTaskDelay(5);

        /* 3. 设置速度限制（2 rad/s，可按需调整） */
        err = robstride_set_csp_speed_limit(id, 2.0f);
        if (err != FSP_SUCCESS) return err;
        vTaskDelay(5);
    }

    /* ---- 夹爪：运控模式（上电默认即为运控模式，直接使能） ---- */
    err = robstride_enable(ROBSTRIDE_MOTOR_ID_GRIPPER);
    if (err != FSP_SUCCESS) return err;
    vTaskDelay(5);

    return FSP_SUCCESS;
}

/**
 * @brief  停止所有电机
 */
static void arm_motors_stop_all(void) {
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT1);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT2);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT3);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT4);
    robstride_stop(ROBSTRIDE_MOTOR_ID_GRIPPER);
}

void can_comms_entry(void *pvParameters){
    FSP_PARAMETER_NOT_USED(pvParameters);

    LOG_I("CAN communication thread started.");

    fsp_err_t err;

    /* ---- 1. 初始化 CAN ---- */
    err = canfd0_init();
    if (err != FSP_SUCCESS){
        LOG_E("CANFD0 initialization failed: %d", err);
        //__BKPT();
    }

    /* ---- 2. 初始化所有电机 ---- */
    err = arm_motors_init();
    if (err != FSP_SUCCESS){
        LOG_E("Failed to initialize arm motors: %d", err);
        //__BKPT();
    }

    can_msg_t tx_msg;

    while(1) {
        if(xQueueReceive(can_tx_queue, &tx_msg, portMAX_DELAY) == pdTRUE) {
            // err = canfd0_tx_process(tx_msg.id, tx_msg.data);
        }
        vTaskDelay(10);
    }
}
