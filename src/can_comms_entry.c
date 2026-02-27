/*
 * can_comms_entry.c
 *
 *   Created on: 2026年1月24日
 *       Author: Ma Ziteng
 *  Description: can通信任务，用于接收电机数据以及发送控制指令。
 *               集成运控模式下的机械臂示教/回放控制。
 */

#include "can_comms_entry.h"
#include "shared_data.h"
#include "sys_log.h"
#include "drv_canfd.h"
#include "robstride_motor.h"
#include "motion_ctrl.h"
#include "nvm_manager.h"
#include <math.h>

/* 控制循环周期（ms） - 匹配 MOTION_CTRL_LOOP_FREQ_HZ (200Hz) */
#define CONTROL_LOOP_PERIOD_MS    5

/**
 * @brief  初始化所有关节电机（运控模式）
 * @return FSP_SUCCESS 或首个错误码
 */
static fsp_err_t arm_motors_init(void) {
    fsp_err_t err;
    const uint8_t joint_ids[5] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4,
        ROBSTRIDE_MOTOR_ID_GRIPPER
    };
    for (uint8_t i = 0; i < 5U; i++) {
        err = robstride_enable(joint_ids[i]);
        if (err != FSP_SUCCESS) return err;
        vTaskDelay(5);
    }
    return FSP_SUCCESS;
}

/**
 * @brief 初始化运控控制器配置
 * @param config 输出配置指针
 */
static void init_motion_config(motion_ctrl_config_t *config) {
    if (config == NULL) return;
    
    /* 使用默认配置 */
    memset(config, 0, sizeof(motion_ctrl_config_t));
    
    /* 示教模式参数 */
    for (int i = 0; i < 4; i++) {
        config->teach.kp[i] = 0.0f;      /* 零刚度 */
        config->teach.kd[i] = 0.5f;      /* 中等阻尼 */
    }
    config->teach.enable_joint1 = true;  /* 默认关节1参与拖动 */
    config->teach.joint1_kd = 0.5f;
    
    /* 回放模式参数 */
    for (int i = 0; i < 4; i++) {
        config->playback.kp[i] = 50.0f;  /* 中等刚度 */
        config->playback.kd[i] = 1.0f;   /* 中等阻尼 */
    }
    
    /* 安全参数 */
    for (int i = 0; i < 4; i++) {
        config->max_torque[i] = 6.0f;    /* 最大力矩 */
        config->max_velocity[i] = 10.0f; /* 最大速度 */
    }
    
    /* 初始化重力补偿参数 */
    grav_init_default(&config->grav_params);
}

/**
 * @brief 运控模式主控制任务
 */
void can_comms_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);
    
    LOG_I("CAN communication thread started.");
    
    fsp_err_t err;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS);
    
    /* 初始化CANFD */
    err = canfd0_init();
    if (err != FSP_SUCCESS) {
        LOG_E("CANFD0 initialization failed: %d", err);
        //__BKPT();
    }
    
    /* 初始化电机 */
    err = arm_motors_init();
    if (err != FSP_SUCCESS) {
        LOG_E("Failed to initialize arm motors: %d", err);
        //__BKPT();
    }
    
    /* 初始化运控控制器 */
    motion_ctrl_config_t motion_config;
    init_motion_config(&motion_config);
    
    if (!motion_ctrl_init(&g_motion_ctrl, &motion_config)) {
        LOG_E("Failed to initialize motion controller");
        //__BKPT();
    }
    
    g_motion_ctrl.state = MOTION_STATE_TEACHING;  /* 默认进入示教模式，等待外部触发 */
    LOG_I("Motion control initialized.");
    
    /* 获取动作配置 */
    const motion_config_t *nvm_motion = nvm_get_motion_config();
    if (nvm_motion == NULL) {
        LOG_W("No motion configuration found in NVM");
    } else {
        LOG_I("Loaded motion configuration from NVM");
    }
    xLastWakeTime = xTaskGetTickCount();
    
    while (1) {
        /* 等待下一个控制周期 */
        xTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        /* 处理急停 */
        if (g_sys_status.is_emergency_stop) {
            if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
                motion_ctrl_emergency_stop(&g_motion_ctrl);
            }
            continue;
        }
        
        if (g_sys_status.is_running) {
            if (g_motion_ctrl.state == MOTION_STATE_TEACHING) {
                if (motion_ctrl_start_teaching(&g_motion_ctrl)) {
                    LOG_I("Teaching mode started");
                }
            }
        } else {
            if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
                motion_ctrl_stop(&g_motion_ctrl);
                LOG_I("Motion control stopped (system not running)");
            }
        }
        
        float dt = (float)CONTROL_LOOP_PERIOD_MS / 1000.0f;  /* 秒 */
        motion_ctrl_loop(&g_motion_ctrl, dt);
        
    }
}