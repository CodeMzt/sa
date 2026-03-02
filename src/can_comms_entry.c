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
#include "voice_command.h"
#include <math.h>

#define PLAYBACK_CHAIN_LEN 3U

/* 回放执行上下文结构体 */
typedef struct {
    bool active;
    bool voice_paused;
    bool queue_warned;
    instrument_t current_inst;
    uint8_t stage;
} playback_exec_ctx_t;

static playback_exec_ctx_t g_playback_ctx = {0};

/* 动作序列映射：FORCEPS->[0,1,6], HEMOSTAT->[2,3,6], SCALPEL->[4,5,6] */
static const uint8_t g_group_chain_map[4][PLAYBACK_CHAIN_LEN] = {
    {0U, 0U, 0U},
    {0U, 1U, 6U},
    {2U, 3U, 6U},
    {4U, 5U, 6U}
};


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
    for (uint8_t i = 0; i < 5U; i++) {
        err = robstride_enable_auto_report(joint_ids[i]);
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
        config->teach.kd[i] = 0.0f;      /* 中等阻尼 */
    }
    config->teach.enable_joint1 = true;  /* 默认关节1参与拖动 */
    config->teach.joint1_kd = 0.5f;
    
    /* 回放模式参数 */
    for (int i = 0; i < 4; i++) {
        config->playback.kp[i] = 50.0f;  /* 中等刚度 */
        config->playback.kd[i] = 1.0f;   /* 中等阻尼 */
    }

    /* IDLE保持模式参数 */
    for (int i = 0; i < 4; i++) {
        config->idle.kp[i] = 10.0f;     /* 刚度 */
        config->idle.kd[i] = 1.0f;       /* 阻尼 */
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
 * @brief 重置回放执行上下文
 */
static void reset_pb_ctx(void) {
    g_playback_ctx.active = false;
    g_playback_ctx.voice_paused = false;
    g_playback_ctx.queue_warned = false;
    g_playback_ctx.current_inst = INSTRUMENT_NONE;
    g_playback_ctx.stage = 0U;
}

/**
 * @brief 回放前暂停语音识别（如有需要）
 * @retval true 已暂停
 * @retval false 无需暂停
 */
static bool pause_voice(void) {
    if (g_sys_status.is_voice_command_running) {
        g_sys_status.is_voice_command_running = false;
        R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
        g_playback_ctx.voice_paused = true;
        LOG_I("Voice recognition paused for playback");
        return true;
    }
    return false;
}

/**
 * @brief 回放结束后恢复语音识别
 */
static void resume_voice(void) {
    if (!g_playback_ctx.voice_paused) {
        return;
    }

    if (g_sys_status.is_running && !g_sys_status.is_emergency_stop) {
        g_sys_status.is_voice_command_running = true;
        R_BSP_IrqEnable(g_i2s0_cfg.rxi_irq);
        LOG_I("Voice recognition resumed after playback");
    }

    g_playback_ctx.voice_paused = false;
}

/**
 * @brief 启动指定动作组的回放
 * @param cfg 运动配置数据
 * @param group_idx 动作组索引
 * @retval true 启动成功
 * @retval false 启动失败
 */
static bool start_group(const motion_config_t *cfg, uint8_t group_idx) {
    if (cfg == NULL || group_idx >= TOTAL_ACTION_GROUPS) {
        return false;
    }

    const action_sequence_t *seq = &cfg->groups[group_idx];
    if (seq->frame_count < 2U || seq->frame_count > MAX_FRAMES_PER_SEQ) {
        LOG_E("Group[%u] invalid frame_count=%u", group_idx, (unsigned int)seq->frame_count);
        return false;
    }

    // 开始回放
    if (!motion_ctrl_start_playback(&g_motion_ctrl, seq)) {
        LOG_E("Failed to start playback for group[%u]", group_idx);
        return false;
    }

    LOG_I("Playback started: group[%u], frames=%u", group_idx, (unsigned int)seq->frame_count);
    return true;
}

/**
 * @brief 载入队首器械，准备回放链
 * @retval true 载入成功
 * @retval false 队列为空或无效
 */
static bool load_next_inst(void) {
    if (g_sys_status.act_queue_count == 0U) {
        return false;
    }

    instrument_t inst = g_sys_status.act_queue[0];
    if (inst < INSTRUMENT_FORCEPS || inst > INSTRUMENT_SCALPEL) {
        LOG_W("Invalid instrument in queue head: %d, removed", inst);
        remove_instrument_from_queue(0U);
        update_queue_display_string();
        return false;
    }

    g_playback_ctx.active = true;
    g_playback_ctx.current_inst = inst;
    g_playback_ctx.stage = 0U;
    g_playback_ctx.queue_warned = false;
    LOG_I("Playback chain begin: inst=%s, queue_count=%u",
          get_instrument_name(inst),
          (unsigned int)g_sys_status.act_queue_count);
    return true;
}

/**
 * @brief 串行调度队列回放流程（touch/voice模式通用）
 * @param cfg 运动配置数据
 */
static void sched_playback(const motion_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }

    /* 正在回放，等待完成 */
    if (g_motion_ctrl.state == MOTION_STATE_PLAYBACK) {
        return;
    }

    /* 示教模式下不进入回放调度 */
    if (g_motion_ctrl.state == MOTION_STATE_TEACHING) {
        return;
    }

    /* 空队列时仅在需要时恢复语音 */
    if (g_sys_status.act_queue_count == 0U && !g_playback_ctx.active) {
        if (!g_playback_ctx.queue_warned && !g_sys_status.is_voice_command_running) {
            LOG_W("START ignored: touch mode queue is empty");
            g_playback_ctx.queue_warned = true;
        }
        resume_voice();
        return;
    }

    /* 取新队首器械并准备三段序列 */
    if (!g_playback_ctx.active) {
        if (!load_next_inst()) {
            return;
        }
        pause_voice();
    }

    /* 当前器械已完成三段，出队并处理下一个 */
    if (g_playback_ctx.stage >= PLAYBACK_CHAIN_LEN) {
        LOG_I("Playback chain completed: inst=%s",
              get_instrument_name(g_playback_ctx.current_inst));
        remove_instrument_from_queue(0U);
        update_queue_display_string();
        g_playback_ctx.active = false;
        g_playback_ctx.current_inst = INSTRUMENT_NONE;
        g_playback_ctx.stage = 0U;

        if (g_sys_status.act_queue_count == 0U) {
            resume_voice();
        }
        return;
    }

    uint8_t group_idx = g_group_chain_map[g_playback_ctx.current_inst][g_playback_ctx.stage];
    LOG_I("Playback stage: inst=%s, stage=%u/%u, group[%u]",
          get_instrument_name(g_playback_ctx.current_inst),
          (unsigned int)(g_playback_ctx.stage + 1U),
          (unsigned int)PLAYBACK_CHAIN_LEN,
          (unsigned int)group_idx);
    if (start_group(cfg, group_idx)) {
        g_playback_ctx.stage++;
    } else {
        LOG_E("Skip instrument %d due to playback start failure", g_playback_ctx.current_inst);
        remove_instrument_from_queue(0U);
        update_queue_display_string();
        g_playback_ctx.active = false;
        g_playback_ctx.current_inst = INSTRUMENT_NONE;
        g_playback_ctx.stage = 0U;
    }
}

void user_on_start(void) {
    g_sys_status.is_running = true;
    LOG_I("System run enabled");

    if (g_sys_status.act_queue_count == 0U) {
        LOG_W("Touch START with empty queue: waiting for queue items");
        g_playback_ctx.queue_warned = true;
    }
}

void user_on_stop(void) {
    g_sys_status.is_running = false;
    LOG_I("System run disabled");
}

void user_on_voice_start(void) {
    g_sys_status.is_running = true;
    g_playback_ctx.queue_warned = false;
    LOG_I("Voice mode start: run enabled");
}

void user_on_voice_stop(void) {
    g_playback_ctx.voice_paused = false;
    LOG_I("Voice mode stopped by user");
}

void user_on_emergency_stop(void) {
    g_sys_status.is_emergency_stop = true;
    g_sys_status.is_running = false;
    g_sys_status.is_voice_command_running = false;
    R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
    reset_pb_ctx();
    LOG_E("Emergency stop requested from UI");
}

void user_on_estop_reset(void) {
    g_sys_status.is_emergency_stop = false;
    LOG_I("Emergency stop reset from UI, software reset triggered");
    NVIC_SystemReset();
}

void user_on_teach_enter(void) {
    if (g_sys_status.is_emergency_stop) {
        LOG_W("Teach mode enter ignored: emergency stop active");
        return;
    }

    g_sys_status.is_running = true;

    if (g_sys_status.is_voice_command_running) {
        g_sys_status.is_voice_command_running = false;
        R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
    }

    reset_pb_ctx();

    if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
        motion_ctrl_stop(&g_motion_ctrl);
    }

    if (!motion_ctrl_start_teaching(&g_motion_ctrl)) {
        LOG_E("Failed to enter teach mode");
        g_sys_status.is_running = false;
        return;
    }

    LOG_I("Teach mode entered");
}

void user_on_teach_exit(void) {
    if (g_motion_ctrl.state == MOTION_STATE_TEACHING) {
        motion_ctrl_stop(&g_motion_ctrl);
    }
    g_sys_status.is_running = false;
    reset_pb_ctx();
    LOG_I("Teach mode exited");
}

/**
 * @brief 运控模式主控制任务
 */
void can_comms_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);
    vTaskDelay(pdMS_TO_TICKS(3000));
    LOG_I("CAN communication thread started.");
    
    fsp_err_t err;
    TickType_t xLastWakeTime;
    
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
    
    g_motion_ctrl.state = MOTION_STATE_IDLE;
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
        float current_period_ms = get_current_mode_period_ms(g_motion_ctrl.state);
        TickType_t xPeriod = pdMS_TO_TICKS(current_period_ms);
        float dt = current_period_ms / 1000.0f;  /* 秒 */
        
        /* 等待下一个控制周期 */
        xTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        /* 处理急停 */
        if (g_sys_status.is_emergency_stop) {
            if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
                motion_ctrl_emergency_stop(&g_motion_ctrl);
            }
            reset_pb_ctx();
            continue;
        }

        /* 系统未运行时保持空闲状态，等待START命令 */
        if (!g_sys_status.is_running) {
            if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
                motion_ctrl_stop(&g_motion_ctrl);
                LOG_I("Motion control stopped (system not running)");
            }

            reset_pb_ctx();
            motion_ctrl_loop(&g_motion_ctrl, dt);
            continue;
        }

        sched_playback(nvm_motion);

        motion_ctrl_loop(&g_motion_ctrl, dt);
        
    }
}
