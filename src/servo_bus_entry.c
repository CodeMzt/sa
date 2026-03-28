#include "servo_bus.h"
#include "shared_data.h"
#include "sys_log.h"
#include "modules/servo/drv_servo.h"
#include "modules/touch/drv_touch.h"
#include "motion_ctrl.h"
#include "nvm_manager.h"
#include "test_mode.h"
#include "voice_command.h"
#include "drv_microphone.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 当前有效任务入口说明                                                        */
/*                                                                            */
/* 本文件是当前实际生效的运控任务入口。它负责：                                 */
/* 1. 初始化串口舵机总线；                                                      */
/* 2. 承担动作队列调度与示教/回放切换；                                         */
/* 3. 保持 UI/WiFi 侧原有回调符号不变。                                         */
/* -------------------------------------------------------------------------- */

#define PLAYBACK_CHAIN_LEN        3U
#define LOG_READY_WAIT_SLICE_MS   10U
#define LOG_READY_WAIT_MAX_MS     1000U
#define SERVO_INIT_RETRY_MS       500U
#define FEEDBACK_PLAYBACK_MS      200U
#define FEEDBACK_IDLE_MS          500U

static uint32_t get_feedback_period_ms(motion_state_t state, bool emergency_stop) {
    if (emergency_stop) {
        return FEEDBACK_IDLE_MS;
    }

    switch (state) {
        case MOTION_STATE_TEACHING:
            return (uint32_t) TEACH_MODE_LOOP_PERIOD_MS;

        case MOTION_STATE_PLAYBACK:
            return FEEDBACK_PLAYBACK_MS;

        case MOTION_STATE_IDLE:
        case MOTION_STATE_ERROR:
        default:
            return FEEDBACK_IDLE_MS;
    }
}

typedef struct {
    bool voice_paused;
    bool queue_warned;
    instrument_t current_inst;
    uint8_t stage;
} playback_exec_ctx_t;

static playback_exec_ctx_t g_playback_ctx = {0};
static void resume_voice(void);

static const uint8_t g_group_chain_map[4][PLAYBACK_CHAIN_LEN] = {
    {0U, 0U, 0U},
    {0U, 1U, 6U},
    {2U, 3U, 6U},
    {4U, 5U, 6U}
};

static bool playback_active(void) {
    return g_playback_ctx.current_inst != INSTRUMENT_NONE;
}

static bool teach_jog_requested(void) {
    return teach_jog_hold_active();
}

static void set_voice_capture(bool enable) {
    g_sys_status.is_voice_command_running = enable;
    if (enable) {
        R_BSP_IrqEnable(g_i2s0_cfg.rxi_irq);
        i2s0_start_rx();
    } else {
        R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
    }
}

/**
 * @brief 重置回放调度上下文
 */
static void reset_pb_ctx(void) {
    g_playback_ctx.voice_paused = false;
    g_playback_ctx.queue_warned = false;
    g_playback_ctx.current_inst = INSTRUMENT_NONE;
    g_playback_ctx.stage = 0U;
    motion_ctrl_reset_handoff_wait(&g_motion_ctrl);
}

static void clear_playback_chain(bool drop_inst, bool resume_if_empty) {
    if (drop_inst) {
        remove_instrument(0U);
        update_queue_display();
    }

    g_playback_ctx.current_inst = INSTRUMENT_NONE;
    g_playback_ctx.stage = 0U;
    motion_ctrl_reset_handoff_wait(&g_motion_ctrl);

    if (resume_if_empty && (queue_count_get() == 0U)) {
        resume_voice();
    }
}

/**
 * @brief 初始化串口舵机机械臂配置与总线
 * @param config 输出给 motion_ctrl 使用的控制参数
 * @retval FSP_SUCCESS 初始化成功
 * @retval 其他错误码 初始化失败
 */
static fsp_err_t servo_arm_init(motion_ctrl_config_t *config) {
    const sys_config_t *sys_cfg = nvm_get_sys_config();

    if (config == NULL) {
        return FSP_ERR_INVALID_POINTER;
    }

    memset(config, 0, sizeof(*config));
    for (uint8_t i = 0U; i < MOTOR_JOINT_NUM; ++i) {
        config->controller.kp[i] = 8.0f;
        config->controller.kd[i] = 0.5f;
        config->max_torque[i] = (sys_cfg != NULL)
                                ? (float) sys_cfg->current_limit[i]
                                : (float) MOTION_DEFAULT_CURRENT_LIMIT_MA;
        config->max_velocity[i] = 0.1f;
    }

    if (!servo_init()) {
        return FSP_ERR_NOT_INITIALIZED;
    }

    fsp_err_t touch_err = touch_drv_init();
    if ((touch_err != FSP_SUCCESS) && (touch_err != FSP_ERR_ALREADY_OPEN)) {
        LOG_W("Touch init failed: %d, auto handoff release disabled until sensor recovers", touch_err);
    }

    return FSP_SUCCESS;
}

/**
 * @brief 当 teach_jog 激活时，强制运行时进入示教状态
 */
static void prepare_teach_jog_context(void) {
    teach_jog_hold_cmd_t hold_jog = {0};
    bool has_hold_jog = teach_jog_hold_active();

    if (!has_hold_jog) return;
    if (has_hold_jog) {
        teach_jog_hold_read(&hold_jog);
    }

    if (g_sys_status.is_voice_command_running) {
        set_voice_capture(false);
    }

    g_sys_status.is_running = false;
    reset_pb_ctx();

    if (g_motion_ctrl.state != MOTION_STATE_TEACHING) {
        if (g_motion_ctrl.state != MOTION_STATE_IDLE) {
            motion_ctrl_stop(&g_motion_ctrl);
        }

        if (!motion_ctrl_start_teaching(&g_motion_ctrl)) {
            LOG_E("Failed to enter teaching mode for teach jog step");
            return;
        }

        if (has_hold_jog && hold_jog.active) {
            teach_jog_hold_set(hold_jog.motor_id,
                               hold_jog.direction,
                               hold_jog.step_level,
                               hold_jog.last_update_tick);
        }
    }
}

/**
 * @brief 在自动回放开始前暂停语音识别
 * @retval true 本次调用成功暂停语音识别
 * @retval false 当前本就未启用语音识别
 */
static bool pause_voice(void) {
    if (g_sys_status.is_voice_command_running) {
        set_voice_capture(false);
        g_playback_ctx.voice_paused = true;
        LOG_I("Voice recognition paused for playback");
        return true;
    }
    return false;
}

/**
 * @brief 在自动回放结束后恢复语音识别
 */
static void resume_voice(void) {
    if (!g_playback_ctx.voice_paused) return;

    if (g_sys_status.is_running && !g_sys_status.is_emergency_stop) {
        set_voice_capture(true);
        LOG_I("Voice recognition resumed after playback");
    }

    g_playback_ctx.voice_paused = false;
}

/**
 * @brief 从持久化动作配置中启动一个动作组
 * @param cfg NVM 中的动作配置
 * @param group_idx 要启动的动作组索引
 * @retval true 启动成功
 * @retval false 动作组无效或启动失败
 */
static bool start_group(const motion_config_t *cfg, uint8_t group_idx) {
    if ((cfg == NULL) || (group_idx >= TOTAL_ACTION_GROUPS)) return false;

    const action_sequence_t *seq = &cfg->groups[group_idx];
    if (!nvm_is_action_sequence_valid(seq, false)) {
        LOG_E("Group[%u] invalid action sequence", group_idx);
        return false;
    }

    if (!motion_ctrl_start_playback(&g_motion_ctrl, seq)) {
        LOG_E("Failed to start playback for group[%u]", group_idx);
        return false;
    }

    LOG_I("Playback started: group[%u], frames=%u", group_idx, (unsigned int) seq->frame_count);
    return true;
}

/**
 * @brief 将队首器械载入回放上下文
 * @retval true 队首器械有效并已载入
 * @retval false 队列为空或队首器械无效
 */
static bool load_next_inst(void) {
    instrument_t inst = INSTRUMENT_NONE;
    uint8_t queue_count = 0U;

    if (!queue_head_peek(&inst, &queue_count)) return false;

    if ((inst < INSTRUMENT_FORCEPS) || (inst > INSTRUMENT_SCALPEL)) {
        LOG_W("Invalid instrument in queue head: %d, removed", inst);
        remove_instrument(0U);
        update_queue_display();
        return false;
    }

    g_playback_ctx.current_inst = inst;
    g_playback_ctx.stage = 0U;
    g_playback_ctx.queue_warned = false;
    motion_ctrl_reset_handoff_wait(&g_motion_ctrl);
    LOG_I("Playback chain begin: inst=%s, queue_count=%u",
          get_instrument_name(inst),
          (unsigned int) queue_count);
    return true;
}

/**
 * @brief 调度器械队列对应的三段式动作链
 * @param cfg NVM 中的动作配置
 */
static void sched_playback(const motion_config_t *cfg) {
    if (cfg == NULL) return;
    if (g_motion_ctrl.state == MOTION_STATE_PLAYBACK) return;
    if (g_motion_ctrl.state == MOTION_STATE_TEACHING) return;
    if (g_motion_ctrl.gripper_action_pause_s > 0.0f) return;

    if ((queue_count_get() == 0U) && !playback_active()) {
        if (!g_playback_ctx.queue_warned && !g_sys_status.is_voice_command_running) {
            LOG_W("START ignored: touch mode queue is empty");
            g_playback_ctx.queue_warned = true;
        }
        resume_voice();
        return;
    }

    if (!playback_active()) {
        if (!load_next_inst()) return;
        pause_voice();
    }

    if (g_playback_ctx.stage >= PLAYBACK_CHAIN_LEN) {
        LOG_I("Playback chain completed: inst=%s", get_instrument_name(g_playback_ctx.current_inst));
        clear_playback_chain(true, true);
        return;
    }

    if (g_playback_ctx.stage == (PLAYBACK_CHAIN_LEN - 1U)) {
        if (g_motion_ctrl.handoff_state == HANDOFF_IDLE) {
            motion_ctrl_arm_handoff_wait(&g_motion_ctrl);
            if (!motion_ctrl_is_handoff_done(&g_motion_ctrl)) {
                LOG_I("Playback paused after second group, waiting for touch handoff release");
                return;
            }
        }

        if (!motion_ctrl_is_handoff_done(&g_motion_ctrl)) {
            return;
        }

        motion_ctrl_reset_handoff_wait(&g_motion_ctrl);
        LOG_I("Touch handoff completed, resuming final playback group");
    }

    uint8_t group_idx = g_group_chain_map[g_playback_ctx.current_inst][g_playback_ctx.stage];
    if (start_group(cfg, group_idx)) {
        g_playback_ctx.stage++;
    } else {
        LOG_E("Skip instrument %d due to playback start failure", g_playback_ctx.current_inst);
        clear_playback_chain(true, false);
    }
}

/* -------------------------------------------------------------------------- */
/* UI / WiFi 兼容回调符号                                                       */
/* 保留 UI 和 WiFi 层原有 user_on_* 符号名，外部调用关系不变；实现已经切换到新的 */
/* servo_bus 运控主链。                                                       */
/* -------------------------------------------------------------------------- */
/**
 * @brief 处理 UI 的开始命令，允许队列进入回放
 */
void user_on_start(void) {
    g_sys_status.is_running = true;
    LOG_I("System run enabled");

    if (queue_count_get() == 0U) {
        LOG_W("Touch START with empty queue: waiting for queue items");
        g_playback_ctx.queue_warned = true;
    }
}

/**
 * @brief 停止队列回放，但保留任务运行
 */
void user_on_stop(void) {
    g_sys_status.is_running = false;
    LOG_I("System run disabled");
}

/**
 * @brief 处理语音模式的启动请求
 */
void user_on_voice_start(void) {
    g_sys_status.is_running = true;
    g_playback_ctx.queue_warned = false;
    LOG_I("Voice mode start: run enabled");
}

/* -------------------------------------------------------------------------- */
/* 兼容回调说明                                                                */
/*                                                                            */
/* 保留 UI 和 WiFi 层历史约定的 user_on_* 符号名，外部调用关系保持不变；        */
/* 但其内部实现已经切换到新的 servo_bus 串口舵机运控主链，不再走旧 CAN 逻辑。   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 标记语音驱动回放已被用户停止
 */
void user_on_voice_stop(void) {
    g_playback_ctx.voice_paused = false;
    LOG_I("Voice mode stopped by user");
}

/**
 * @brief 由 UI 回调触发全局急停
 */
void user_on_emergency_stop(void) {
    g_sys_status.is_emergency_stop = true;
    g_sys_status.is_running = false;
    set_voice_capture(false);
    motion_ctrl_clear_teach_jog(&g_motion_ctrl, true);
    reset_pb_ctx();
    LOG_E("Emergency stop requested from UI");
}

/**
 * @brief 通过整机复位清除急停状态
 */
void user_on_estop_reset(void) {
    g_sys_status.is_emergency_stop = false;
    LOG_I("Emergency stop reset from UI, software reset triggered");
    NVIC_SystemReset();
}

/**
 * @brief 从 UI 进入示教模式，同时暂停语音采集
 */
void user_on_teach_enter(void) {
    if (g_sys_status.is_voice_command_running) {
        set_voice_capture(false);
    }

    if (!motion_ctrl_start_teaching(&g_motion_ctrl)) {
        LOG_W("Teach enter ignored in state %d", g_motion_ctrl.state);
        return;
    }

    g_sys_status.is_running = false;
    reset_pb_ctx();
    LOG_I("Teach mode entered");
}

/**
 * @brief 退出示教模式，并回到 IDLE 姿态保持
 */
void user_on_teach_exit(void) {
    motion_ctrl_clear_teach_jog(&g_motion_ctrl, true);
    motion_ctrl_stop(&g_motion_ctrl);
    LOG_I("Teach mode exited");
}

/**
 * @brief 串口舵机运控任务入口
 * @param pvParameters 未使用的 FreeRTOS 任务参数
 */
void servo_bus_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_CAN_COMMS
    LOG_I("Test mode: servo_bus thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    uint32_t wait_ms = 0U;
    while ((!g_log_system_ready) && (wait_ms < LOG_READY_WAIT_MAX_MS)) {
        vTaskDelay(pdMS_TO_TICKS(LOG_READY_WAIT_SLICE_MS));
        wait_ms += LOG_READY_WAIT_SLICE_MS;
    }

    fsp_err_t nvm_err = nvm_init();
    if (nvm_err != FSP_SUCCESS) {
        LOG_E("NVM init failed in servo task: %d", nvm_err);
    }

    motion_ctrl_config_t motion_config;
    fsp_err_t err = FSP_ERR_NOT_INITIALIZED;
    while ((err = servo_arm_init(&motion_config)) != FSP_SUCCESS) {
        motion_link_set(false);
        LOG_E("Failed to initialize serial servos: %d, retry in %u ms", err, SERVO_INIT_RETRY_MS);
        vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_RETRY_MS));
    }

    if (!motion_ctrl_init(&g_motion_ctrl, &motion_config)) {
        LOG_E("Failed to initialize motion controller");
        vTaskDelete(NULL);
        return;
    }

    g_motion_ctrl.state = MOTION_STATE_IDLE;
    LOG_I("Servo bus control thread started.");

    const motion_config_t *nvm_motion = nvm_get_motion_config();
    if (nvm_motion == NULL) {
        LOG_W("No motion configuration found in NVM");
    }

    TickType_t x_last_wake_time = xTaskGetTickCount();
    TickType_t x_prev_loop_tick = x_last_wake_time;
    TickType_t x_last_feedback_poll_tick = x_last_wake_time;
    uint32_t rx_fail_log_div = 0U;
    bool link_warned = false;

    while (1) {
        float current_period_ms = get_ctrl_period(g_motion_ctrl.state);
        TickType_t x_period = pdMS_TO_TICKS(current_period_ms);
        float dt = current_period_ms / 1000.0f;

        xTaskDelayUntil(&x_last_wake_time, x_period);
        TickType_t x_now_tick = xTaskGetTickCount();
        TickType_t x_delta_tick = x_now_tick - x_prev_loop_tick;
        x_prev_loop_tick = x_now_tick;
        if (x_delta_tick > 0U) {
            dt = (float) x_delta_tick / (float) configTICK_RATE_HZ;
        }

        TickType_t feedback_period_ticks =
            pdMS_TO_TICKS(get_feedback_period_ms(g_motion_ctrl.state, g_sys_status.is_emergency_stop));
        if ((g_motion_ctrl.state != MOTION_STATE_PLAYBACK) &&
            ((x_now_tick - x_last_feedback_poll_tick) >= feedback_period_ticks)) {
            bool need_full_feedback =
                teach_jog_requested() ||
                (g_motion_ctrl.state == MOTION_STATE_TEACHING);
            fsp_err_t refresh_err = need_full_feedback
                                    ? servo_refresh_feedback()
                                    : servo_refresh_joint_feedback();
            x_last_feedback_poll_tick = x_now_tick;

            if (refresh_err != FSP_SUCCESS) {
                if ((rx_fail_log_div++ % 20U) == 0U) {
                    LOG_W("Servo feedback refresh failed");
                }
            } else {
                rx_fail_log_div = 0U;
            }
        }

        servo_link_check();
        if (!servo_is_connected()) {
            if (!link_warned) {
                LOG_W("Motion link lost, controller forced to zero-force mode");
                servo_log_link_diagnostics();
                link_warned = true;
            }

            motion_ctrl_clear_teach_jog(&g_motion_ctrl, false);
            if (!g_motion_ctrl.zero_force_mode) {
                motion_ctrl_enter_zero_force(&g_motion_ctrl);
            }
            reset_pb_ctx();
            continue;
        }
        link_warned = false;

        if (g_sys_status.is_emergency_stop) {
            if (!g_motion_ctrl.emergency_stop) {
                motion_ctrl_emergency_stop(&g_motion_ctrl);
            }
            motion_ctrl_clear_teach_jog(&g_motion_ctrl, true);
            reset_pb_ctx();
            continue;
        }

        if (teach_jog_requested() || (g_motion_ctrl.state == MOTION_STATE_TEACHING)) {
            prepare_teach_jog_context();
            motion_ctrl_loop(&g_motion_ctrl, dt);
            continue;
        }

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
