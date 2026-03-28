/**
 * @file drv_servo.c
 * @brief 串口舵机总线驱动实现
 * @author Ma Ziteng 
 */

/* -------------------------------------------------------------------------- */
/* 当前有效底层驱动说明                                                        */
/*                                                                            */
/* 本文件已根据 `reference/servo/servo-bus-r.pdf` 修正为公共标准版串口舵机协议 v4.01： */
/* 1. 控制器下发帧头为 `FF FF`，应答帧头为 `FF F5`；                           */
/* 2. 多字节数据统一按大端处理；                                               */
/* 3. 关节位置控制使用 `0x2A/0x2C + 0x83`；                                    */
/* 4. 反馈通过单舵机 `READ DATA` 读取当前位置/电流/温度；                       */
/* 5. 协议未提供旧版 0x59/0x82 自定义批量反馈链路，因此本文件不再依赖该路径。   */
/* -------------------------------------------------------------------------- */

#include "drv_servo.h"
#include "nvm_manager.h"
#include "nvm_types.h"
#include "servo_bus.h"
#include "shared_data.h"
#include "sys_log.h"
#include <stdio.h>
#include <string.h>

#define SERVO_HEADER_1               (0xFFU)
#define SERVO_HEADER_TX_2            (0xFFU)
#define SERVO_HEADER_RX_2            (0xF5U)
#define SERVO_ID_BROADCAST           (0xFEU)

#define SERVO_CMD_PING               (0x01U)
#define SERVO_CMD_READ               (0x02U)
#define SERVO_CMD_WRITE              (0x03U)
#define SERVO_CMD_REG_WRITE          (0x04U)
#define SERVO_CMD_ACTION             (0x05U)
#define SERVO_CMD_MULTI_WRITE        (0x83U)

#define SERVO_ADDR_MAX_TORQUE        (0x10U)
#define SERVO_ADDR_TORQUE_SWITCH     (0x28U)
#define SERVO_ADDR_TARGET_POSITION   (0x2AU)
#define SERVO_ADDR_TARGET_TIME       (0x2CU)
#define SERVO_ADDR_CURRENT_CURRENT   (0x2EU)
#define SERVO_ADDR_LOCK_FLAG         (0x30U)
#define SERVO_ADDR_CURRENT_POSITION  (0x38U)
#define SERVO_ADDR_CURRENT_SPEED     (0x3AU)
#define SERVO_ADDR_CURRENT_TEMP      (0x3FU)

/* reference/servo/servo-bus-r.pdf v4.01 section 2.4: response status byte bit definitions */
#define SERVO_STATUS_VOLTAGE_ABNORMAL (1U << 0)
#define SERVO_STATUS_TEMP_ABNORMAL    (1U << 2)
#define SERVO_STATUS_OVERLOAD_STALL   (1U << 5)

#define SERVO_TORQUE_OFF             (0U)
#define SERVO_TORQUE_ON              (1U)
#define SERVO_LOCK_OFF               (0U)
#define SERVO_LOCK_ON                (1U)

#define SERVO_RX_RING_SIZE           (512U)
#define SERVO_FRAME_MAX_SIZE         (64U)
#define SERVO_TX_TIMEOUT_MS          (20U)
#define SERVO_RX_TIMEOUT_MS          (20U)
#define SERVO_SENSOR_RX_TIMEOUT_MS   (80U)
#define SERVO_LINK_FAIL_LIMIT        (3U)
#define SERVO_INIT_SETTLE_MS         (80U)
#define SERVO_INIT_RETRY_COUNT       (3U)
#define SERVO_INIT_RETRY_DELAY_MS    (30U)
#define SERVO_INTER_READ_GAP_MS      (2U)
#define SERVO_SENSOR_READ_GAP_MS     (10U)
#define SERVO_SENSOR_RETRY_COUNT     (2U)
#define SERVO_SENSOR_RETRY_DELAY_MS  (5U)
#define SERVO_JOINT_MOVE_TIME_MS     (40U)
#define SERVO_GRIPPER_MOVE_TIME_MS   (20U)
#define SERVO_GRIPPER_LIMIT_MAX_MA   (MOTION_DEFAULT_CURRENT_LIMIT_MA)
#define SERVO_CURRENT_REFRESH_DIV    (10U)
#define SERVO_TEMP_REFRESH_DIV       (100U)
#define SERVO_SENSOR_WARN_INTERVAL_MS (5000U)

#define DEG2RAD_F                    (0.01745329251994329577f)
#define RAD2DEG_F                    (57.2957795130823208768f)
#define SERVO_POS_MIN_CMD            (96U)
#define SERVO_POS_CENTER_CMD         (2048.0f)
#define SERVO_POS_MAX_CMD            (4000U)
#define SERVO_POS_SPAN_CMD_F         ((float) (SERVO_POS_MAX_CMD - SERVO_POS_MIN_CMD))
#define SERVO_POS_RANGE_DEG          (300.0f)
#define SERVO_DEG_PER_CMD_F          (SERVO_POS_RANGE_DEG / SERVO_POS_SPAN_CMD_F)
#define SERVO_GRIPPER_CLOSED_CMD     (2600U)
#define SERVO_GRIPPER_RELEASE_CMD    (2048U)
/* 新协议中用 0x2E“当前电流(mA)”作为保护和显示的力矩代理量。 */
#define SERVO_TORQUE_PROXY_SCALE_F   (1.0f)

/* 当前运行时控制关节 1~5。 */
static const uint8_t k_joint_ids[MOTOR_ACTIVE_JOINT_NUM] = {
    MOTOR_ID_JOINT1,
    MOTOR_ID_JOINT2,
    MOTOR_ID_JOINT3,
    MOTOR_ID_JOINT4,
    MOTOR_ID_JOINT5
};

static const uint8_t k_all_motor_ids[MOTOR_ACTIVE_MOTOR_NUM] = {
    MOTOR_ID_JOINT1,
    MOTOR_ID_JOINT2,
    MOTOR_ID_JOINT3,
    MOTOR_ID_JOINT4,
    MOTOR_ID_JOINT5,
    MOTOR_ID_GRIPPER
};

static SemaphoreHandle_t g_servo_mutex;
static StaticSemaphore_t g_servo_mutex_memory;

static volatile uint8_t g_rx_ring[SERVO_RX_RING_SIZE];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;
static volatile bool g_uart_tx_done = true;
static volatile bool g_uart_error = false;
static volatile bool g_servo_uart_open = false;
static volatile bool g_servo_connected = false;
static volatile uint32_t g_last_feedback_tick = 0U;
static volatile uint8_t g_feedback_fail_count = 0U;
static uint32_t g_feedback_cycle = 0U;
static uint8_t g_current_refresh_index = 0U;
static uint8_t g_temp_refresh_index = 0U;
static uint16_t g_gripper_torque_limit_cache = 0U;
static bool g_gripper_torque_limit_valid = false;

static float g_last_position_fb[MOTOR_NUM];
static uint32_t g_last_position_tick[MOTOR_NUM];
static bool g_position_fb_valid[MOTOR_NUM];
static uint32_t g_last_joint_feedback_tick[MOTOR_ACTIVE_JOINT_NUM];
static uint32_t g_last_sensor_trace_tick[MOTOR_NUM];

typedef enum {
    SERVO_REFRESH_MODE_FULL = 0,
    SERVO_REFRESH_MODE_JOINTS_ONLY
} servo_refresh_mode_t;

/**
 * @brief 获取舵机锁
 */
static fsp_err_t servo_take_lock(uint32_t timeout_ms) {
    if (g_servo_mutex == NULL) {
        return FSP_ERR_NOT_INITIALIZED;
    }

    return (xSemaphoreTake(g_servo_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
           ? FSP_SUCCESS
           : FSP_ERR_IN_USE;
}


/* -------------------------------------------------------------------------- */
/* 本地工具函数                                                                */
/* -------------------------------------------------------------------------- */
static inline uint32_t servo_now_ticks(void) {
    return (uint32_t) xTaskGetTickCount();
}

/**
 * @brief 判断给定电机 ID 是否为当前控制的有效关节电机，并返回对应的关节索引
 */
static inline bool servo_motor_is_active_joint(uint8_t motor_id, uint8_t *joint_index) {
    if ((motor_id < MOTOR_ID_JOINT1) || (motor_id > MOTOR_ID_JOINT5)) {
        return false;
    }

    if (joint_index != NULL) {
        *joint_index = (uint8_t) (motor_id - MOTOR_ID_JOINT1);
    }

    return true;
}

/**
 * @brief 将弧度位置转换为舵机位置命令值，并进行限幅
 */
static inline int32_t servo_round_to_i32(float value) {
    return (int32_t) ((value >= 0.0f) ? (value + 0.5f) : (value - 0.5f));
}

/**
 * @brief 将整数值限制在指定的无符号 16 位范围内
 */
static inline uint16_t servo_clamp_u16(int32_t value, uint16_t min_value, uint16_t max_value) {
    if (value < (int32_t) min_value) return min_value;
    if (value > (int32_t) max_value) return max_value;
    return (uint16_t) value;
}

/**
 * @brief 将 gripper 的位置命令值限制在协议定义的范围内
 */
static inline uint16_t servo_gripper_clamp_target_cmd(int32_t value) {
    uint16_t min_cmd = (SERVO_GRIPPER_RELEASE_CMD < SERVO_GRIPPER_CLOSED_CMD)
                       ? SERVO_GRIPPER_RELEASE_CMD
                       : SERVO_GRIPPER_CLOSED_CMD;
    uint16_t max_cmd = (SERVO_GRIPPER_RELEASE_CMD > SERVO_GRIPPER_CLOSED_CMD)
                       ? SERVO_GRIPPER_RELEASE_CMD
                       : SERVO_GRIPPER_CLOSED_CMD;
    return servo_clamp_u16(value, min_cmd, max_cmd);
}

/**
 * @brief 获取 gripper 的电流限制值（mA）
 */
static uint16_t servo_get_gripper_current_limit_ma(void) {
    const sys_config_t *sys_cfg = nvm_get_sys_config();

    if ((sys_cfg != NULL) && (sys_cfg->current_limit[4] > 0U)) {
        return sys_cfg->current_limit[4];
    }

    return MOTION_DEFAULT_CURRENT_LIMIT_MA;
}

/**
 * @brief 环形缓冲区下一个索引位置
 */
static inline uint16_t servo_ring_next(uint16_t index) {
    return (uint16_t) ((index + 1U) % SERVO_RX_RING_SIZE);
}

static void servo_ring_clear(void) {
    taskENTER_CRITICAL();
    g_rx_head = 0U;
    g_rx_tail = 0U;
    taskEXIT_CRITICAL();
}

/**
 * @brief 串口接收中断服务程序，单字节入环形缓冲区
 */
static void servo_ring_push_isr(uint8_t byte) {
    uint16_t next = servo_ring_next(g_rx_head);

    if (next == g_rx_tail) {
        g_rx_tail = servo_ring_next(g_rx_tail);
    }

    g_rx_ring[g_rx_head] = byte;
    g_rx_head = next;
}

/**
 * @brief 获取环形缓冲区中可用字节数
 */
static uint16_t servo_ring_count(void) {
    uint16_t head = g_rx_head;
    uint16_t tail = g_rx_tail;
    if (head >= tail) return (uint16_t) (head - tail);
    return (uint16_t) (SERVO_RX_RING_SIZE - tail + head);
}

static uint8_t servo_ring_peek(uint16_t offset) {
    return g_rx_ring[(g_rx_tail + offset) % SERVO_RX_RING_SIZE];
}

static void servo_ring_drop(uint16_t count) {
    while ((count-- > 0U) && (g_rx_tail != g_rx_head)) {
        g_rx_tail = servo_ring_next(g_rx_tail);
    }
}

static void servo_ring_pop(uint8_t *dst, uint16_t count) {
    for (uint16_t i = 0U; i < count; ++i) {
        if (g_rx_tail == g_rx_head) {
            return;
        }

        dst[i] = g_rx_ring[g_rx_tail];
        g_rx_tail = servo_ring_next(g_rx_tail);
    }
}

static int16_t servo_rad_to_cmd(float rad) {
    float deg = rad * RAD2DEG_F;
    float cmd = (deg / SERVO_DEG_PER_CMD_F) + SERVO_POS_CENTER_CMD;
    return (int16_t) servo_clamp_u16(servo_round_to_i32(cmd), SERVO_POS_MIN_CMD, SERVO_POS_MAX_CMD);
}

static float servo_cmd_to_rad(uint16_t pos_cmd) {
    float deg = ((float) servo_clamp_u16((int32_t) pos_cmd, SERVO_POS_MIN_CMD, SERVO_POS_MAX_CMD)
        - SERVO_POS_CENTER_CMD) * SERVO_DEG_PER_CMD_F;
    return deg * DEG2RAD_F;
}

static fsp_err_t servo_read_data_locked(uint8_t motor_id, uint8_t adr, uint8_t *payload, uint8_t payload_len);
static fsp_err_t servo_read_data_timeout_locked(uint8_t motor_id,
                                                uint8_t adr,
                                                uint8_t *payload,
                                                uint8_t payload_len,
                                                uint32_t timeout_ms);
static fsp_err_t servo_read_sensor_data_locked(uint8_t motor_id,
                                               uint8_t adr,
                                               uint8_t *payload,
                                               uint8_t payload_len);
static void servo_log_motor_diag_locked(uint8_t motor_id);
static void servo_log_sensor_timeout_trace_locked(uint8_t motor_id,
                                                  uint8_t adr,
                                                  uint8_t payload_len,
                                                  uint32_t timeout_ms);

static void servo_inter_read_gap(void) {
    if (SERVO_INTER_READ_GAP_MS > 0U) {
        vTaskDelay(pdMS_TO_TICKS(SERVO_INTER_READ_GAP_MS));
    }
}

static void servo_sensor_read_gap(void) {
    if (SERVO_SENSOR_READ_GAP_MS > 0U) {
        vTaskDelay(pdMS_TO_TICKS(SERVO_SENSOR_READ_GAP_MS));
    }
}

/* -------------------------------------------------------------------------- */
/* 协议封包辅助函数                                                            */
/* -------------------------------------------------------------------------- */
static uint8_t servo_calc_checksum(const uint8_t *frame, uint16_t payload_len) {
    uint32_t sum = 0U;

    for (uint16_t i = 2U; i < (uint16_t) (2U + payload_len); ++i) {
        sum += frame[i];
    }

    return (uint8_t) (~(sum & 0xFFU));
}

static uint16_t servo_build_frame(uint8_t *frame,
                                  uint8_t id,
                                  uint8_t cmd,
                                  uint8_t adr,
                                  const uint8_t *params,
                                  uint8_t param_len) {
    uint8_t length = (uint8_t) (param_len + 3U);
    uint16_t total_len = (uint16_t) (length + 4U);

    frame[0] = SERVO_HEADER_1;
    frame[1] = SERVO_HEADER_TX_2;
    frame[2] = id;
    frame[3] = length;
    frame[4] = cmd;
    frame[5] = adr;

    if ((params != NULL) && (param_len > 0U)) {
        memcpy(&frame[6], params, param_len);
    }

    frame[total_len - 1U] = servo_calc_checksum(frame, (uint16_t) (length + 1U));
    return total_len;
}

static bool servo_wait_tx_done(uint32_t timeout_ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (!g_uart_tx_done) {
        if ((int32_t) (xTaskGetTickCount() - deadline) >= 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return true;
}

static bool servo_wait_rx_bytes(uint16_t min_bytes, uint32_t timeout_ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (servo_ring_count() < min_bytes) {
        if ((int32_t) (xTaskGetTickCount() - deadline) >= 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return true;
}

static bool servo_extract_frame(uint8_t *frame, uint16_t *frame_len, uint32_t timeout_ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while ((int32_t) (xTaskGetTickCount() - deadline) < 0) {
        while (servo_ring_count() >= 2U) {
            if (servo_ring_peek(0U) != SERVO_HEADER_1) {
                servo_ring_drop(1U);
                continue;
            }

            if (servo_ring_peek(1U) != SERVO_HEADER_RX_2) {
                servo_ring_drop(1U);
                continue;
            }

            if (!servo_wait_rx_bytes(4U, timeout_ms)) {
                break;
            }

            uint16_t total_len = (uint16_t) (servo_ring_peek(3U) + 4U);
            if ((total_len < 6U) || (total_len > SERVO_FRAME_MAX_SIZE)) {
                servo_ring_drop(1U);
                continue;
            }

            if (!servo_wait_rx_bytes(total_len, timeout_ms)) {
                break;
            }

            servo_ring_pop(frame, total_len);
            if (frame[total_len - 1U] != servo_calc_checksum(frame, (uint16_t) (frame[3] + 1U))) {
                continue;
            }

            *frame_len = total_len;
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Locked Bus Transactions                                                    */
/* -------------------------------------------------------------------------- */
static fsp_err_t servo_send_frame_locked(const uint8_t *frame, uint16_t frame_len) {
    if (!g_servo_uart_open) return FSP_ERR_NOT_OPEN;

    g_uart_tx_done = false;
    g_uart_error = false;

    fsp_err_t err = g_uart_servo.p_api->write(g_uart_servo.p_ctrl, (uint8_t *) frame, frame_len);
    if (err != FSP_SUCCESS) {
        g_uart_tx_done = true;
        return err;
    }

    if (!servo_wait_tx_done(SERVO_TX_TIMEOUT_MS)) {
        return FSP_ERR_TIMEOUT;
    }

    if (g_uart_error) {
        return FSP_ERR_INVALID_DATA;
    }

    return FSP_SUCCESS;
}

static fsp_err_t servo_write_u8_locked(uint8_t motor_id, uint8_t adr, uint8_t value) {
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint16_t frame_len = servo_build_frame(frame, motor_id, SERVO_CMD_WRITE, adr, &value, 1U);
    servo_ring_clear();
    return servo_send_frame_locked(frame, frame_len);
}

static fsp_err_t servo_write_u16_locked(uint8_t motor_id, uint8_t adr, uint16_t value) {
    uint8_t params[2] = {
        (uint8_t) ((value >> 8) & 0xFFU),
        (uint8_t) (value & 0xFFU)
    };
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint16_t frame_len = servo_build_frame(frame, motor_id, SERVO_CMD_WRITE, adr, params, sizeof(params));
    servo_ring_clear();
    return servo_send_frame_locked(frame, frame_len);
}

static fsp_err_t servo_write_raw_locked(uint8_t motor_id, uint8_t adr, const uint8_t *params, uint8_t param_len) {
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint16_t frame_len = servo_build_frame(frame, motor_id, SERVO_CMD_WRITE, adr, params, param_len);
    servo_ring_clear();
    return servo_send_frame_locked(frame, frame_len);
}

static fsp_err_t servo_ping_locked(uint8_t motor_id) {
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint8_t rx_frame[SERVO_FRAME_MAX_SIZE];
    uint16_t tx_len = 6U;
    uint16_t rx_len = 0U;

    frame[0] = SERVO_HEADER_1;
    frame[1] = SERVO_HEADER_TX_2;
    frame[2] = motor_id;
    frame[3] = 2U;
    frame[4] = SERVO_CMD_PING;
    frame[5] = servo_calc_checksum(frame, 3U);

    servo_ring_clear();

    fsp_err_t err = servo_send_frame_locked(frame, tx_len);
    if (err != FSP_SUCCESS) {
        return err;
    }

    if (!servo_extract_frame(rx_frame, &rx_len, SERVO_RX_TIMEOUT_MS)) {
        return FSP_ERR_TIMEOUT;
    }

    if ((rx_len < 6U) || (rx_frame[2] != motor_id)) {
        return FSP_ERR_INVALID_DATA;
    }

    return FSP_SUCCESS;
}

static fsp_err_t servo_probe_motor_locked(uint8_t motor_id) {
    fsp_err_t err = servo_ping_locked(motor_id);
    if (err == FSP_SUCCESS) {
        return FSP_SUCCESS;
    }

    /* Some servos respond reliably to READ DATA but not to PING. Fall back to
     * a position read so startup does not fail on an unsupported ping command.
     */
    uint8_t pos_payload[2] = {0};
    err = servo_read_data_locked(motor_id, SERVO_ADDR_CURRENT_POSITION, pos_payload, sizeof(pos_payload));
    if (err == FSP_SUCCESS) {
        LOG_W("Servo[%u] ping failed, position read fallback succeeded", motor_id);
    }

    return err;
}

static fsp_err_t servo_probe_motor_retry_locked(uint8_t motor_id) {
    fsp_err_t err = FSP_ERR_NOT_INITIALIZED;

    for (uint8_t attempt = 0U; attempt < SERVO_INIT_RETRY_COUNT; ++attempt) {
        err = servo_probe_motor_locked(motor_id);
        if (err == FSP_SUCCESS) {
            return FSP_SUCCESS;
        }

        if ((attempt + 1U) < SERVO_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_RETRY_DELAY_MS));
        }
    }

    return err;
}

static fsp_err_t servo_read_data_locked(uint8_t motor_id, uint8_t adr, uint8_t *payload, uint8_t payload_len) {
    return servo_read_data_timeout_locked(motor_id, adr, payload, payload_len, SERVO_RX_TIMEOUT_MS);
}

static fsp_err_t servo_read_data_timeout_locked(uint8_t motor_id,
                                                uint8_t adr,
                                                uint8_t *payload,
                                                uint8_t payload_len,
                                                uint32_t timeout_ms) {
    uint8_t params[1] = {payload_len};
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint8_t rx_frame[SERVO_FRAME_MAX_SIZE];
    uint16_t tx_len = servo_build_frame(frame, motor_id, SERVO_CMD_READ, adr, params, sizeof(params));
    uint16_t rx_len = 0U;

    if ((payload == NULL) || (payload_len == 0U)) {
        return FSP_ERR_INVALID_POINTER;
    }

    servo_ring_clear();

    fsp_err_t err = servo_send_frame_locked(frame, tx_len);
    if (err != FSP_SUCCESS) {
        return err;
    }

    if (!servo_extract_frame(rx_frame, &rx_len, timeout_ms)) {
        if ((adr == SERVO_ADDR_CURRENT_CURRENT) || (adr == SERVO_ADDR_CURRENT_TEMP)) {
            servo_log_sensor_timeout_trace_locked(motor_id, adr, payload_len, timeout_ms);
        }
        return FSP_ERR_TIMEOUT;
    }

    if ((rx_len < 6U) || (rx_frame[2] != motor_id)) {
        return FSP_ERR_INVALID_DATA;
    }

    uint8_t motor_index = motor_get_index(motor_id);
    if (motor_index < MOTOR_NUM) {
        /* reference/servo/servo-bus-r.pdf v4.01 section 2.4 defines this response byte as the
         * serial servo status byte. Its meaning is not the same as the legacy
         * CAN fault bitmap, so callers must not reuse the legacy interpretation
         * here.
         */
        g_motors[motor_index].feedback.fault_flags = rx_frame[4];
    }

    if ((uint8_t) (rx_frame[3] - 2U) < payload_len) {
        return FSP_ERR_INVALID_SIZE;
    }

    memcpy(payload, &rx_frame[5], payload_len);
    return FSP_SUCCESS;
}

static fsp_err_t servo_read_sensor_data_locked(uint8_t motor_id,
                                               uint8_t adr,
                                               uint8_t *payload,
                                               uint8_t payload_len) {
    fsp_err_t err = FSP_ERR_TIMEOUT;

    for (uint8_t attempt = 0U; attempt < SERVO_SENSOR_RETRY_COUNT; ++attempt) {
        err = servo_read_data_timeout_locked(motor_id,
                                             adr,
                                             payload,
                                             payload_len,
                                             SERVO_SENSOR_RX_TIMEOUT_MS);
        if (err == FSP_SUCCESS) {
            return FSP_SUCCESS;
        }

        if ((attempt + 1U) < SERVO_SENSOR_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(SERVO_SENSOR_RETRY_DELAY_MS));
        }
    }

    return err;
}

static void servo_log_motor_diag_locked(uint8_t motor_id) {
    uint8_t pos_payload[2] = {0};
    uint8_t cur_payload[2] = {0};
    uint8_t temp_payload[1] = {0};
    uint8_t lock_payload[1] = {0};
    uint8_t torque_payload[1] = {0};
    fsp_err_t pos_err = servo_read_data_locked(motor_id, SERVO_ADDR_CURRENT_POSITION, pos_payload, sizeof(pos_payload));
    if (pos_err == FSP_SUCCESS) {
        servo_inter_read_gap();
    }
    servo_sensor_read_gap();
    fsp_err_t cur_err = servo_read_sensor_data_locked(motor_id,
                                                      SERVO_ADDR_CURRENT_CURRENT,
                                                      cur_payload,
                                                      sizeof(cur_payload));
    servo_sensor_read_gap();
    fsp_err_t temp_err = servo_read_sensor_data_locked(motor_id,
                                                       SERVO_ADDR_CURRENT_TEMP,
                                                       temp_payload,
                                                       sizeof(temp_payload));
    servo_inter_read_gap();
    fsp_err_t lock_err = servo_read_data_locked(motor_id, SERVO_ADDR_LOCK_FLAG, lock_payload, sizeof(lock_payload));
    servo_inter_read_gap();
    fsp_err_t torque_err = servo_read_data_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, torque_payload, sizeof(torque_payload));
    uint8_t motor_index = motor_get_index(motor_id);
    uint16_t pos_cmd = 0U;
    uint16_t cur_raw = 0U;
    uint8_t temp_raw = 0U;
    uint8_t lock_flag = 0U;
    uint8_t torque_en = 0U;
    float pos_deg = 0.0f;
    float cached_pos_deg = 0.0f;
    float cur_proxy = 0.0f;
    float temp_c = 0.0f;
    uint8_t status_byte = 0U;
    bool voltage_abnormal = false;
    bool temp_abnormal = false;
    bool overload_stall = false;

    if (pos_err == FSP_SUCCESS) {
        pos_cmd = (uint16_t) (((uint16_t) pos_payload[0] << 8) | (uint16_t) pos_payload[1]);
        pos_deg = servo_cmd_to_rad(pos_cmd) * RAD2DEG_F;
    }
    if (cur_err == FSP_SUCCESS) {
        cur_raw = (uint16_t) (((uint16_t) cur_payload[0] << 8) | (uint16_t) cur_payload[1]);
    }
    if (temp_err == FSP_SUCCESS) {
        temp_raw = temp_payload[0];
    }
    if (lock_err == FSP_SUCCESS) {
        lock_flag = lock_payload[0];
    }
    if (torque_err == FSP_SUCCESS) {
        torque_en = torque_payload[0];
    }

    if (motor_index < MOTOR_NUM) {
        cached_pos_deg = g_motors[motor_index].feedback.position * RAD2DEG_F;
        cur_proxy = g_motors[motor_index].feedback.torque;
        temp_c = g_motors[motor_index].feedback.temperature;
        status_byte = g_motors[motor_index].feedback.fault_flags;
    }
    voltage_abnormal = ((status_byte & SERVO_STATUS_VOLTAGE_ABNORMAL) != 0U);
    temp_abnormal = ((status_byte & SERVO_STATUS_TEMP_ABNORMAL) != 0U);
    overload_stall = ((status_byte & SERVO_STATUS_OVERLOAD_STALL) != 0U);

    LOG_W("Servo[%u] diag: raw_status=0x%02X volt_abn=%u temp_abn=%u overload=%u pos_err=%d pos_cmd=%u pos_deg=%.2f cur_err=%d cur_raw=%u temp_err=%d temp_raw=%u lock_err=%d lock=%u torque_err=%d torque_en=%u cached_pos_deg=%.2f cached_cur=%.1f cached_temp=%.1f",
          (unsigned int) motor_id,
          (unsigned int) status_byte,
          (unsigned int) voltage_abnormal,
          (unsigned int) temp_abnormal,
          (unsigned int) overload_stall,
          pos_err,
          (unsigned int) pos_cmd,
          pos_deg,
          cur_err,
          (unsigned int) cur_raw,
          temp_err,
          (unsigned int) temp_raw,
          lock_err,
          (unsigned int) lock_flag,
          torque_err,
          (unsigned int) torque_en,
          cached_pos_deg,
          cur_proxy,
          temp_c);
}

static void servo_log_sensor_timeout_trace_locked(uint8_t motor_id,
                                                  uint8_t adr,
                                                  uint8_t payload_len,
                                                  uint32_t timeout_ms) {
    uint8_t motor_index = motor_get_index(motor_id);
    if (motor_index >= MOTOR_NUM) {
        return;
    }

    uint32_t now_tick = servo_now_ticks();
    uint32_t last_tick = g_last_sensor_trace_tick[motor_index];
    if ((last_tick != 0U) &&
        ((now_tick - last_tick) < pdMS_TO_TICKS(SERVO_SENSOR_WARN_INTERVAL_MS))) {
        return;
    }
    g_last_sensor_trace_tick[motor_index] = now_tick;

    uint16_t rx_count = servo_ring_count();
    uint16_t dump_count = (rx_count > 12U) ? 12U : rx_count;
    char dump_buf[3U * 12U + 1U];
    uint16_t pos = 0U;

    for (uint16_t i = 0U; i < dump_count; ++i) {
        uint8_t byte = servo_ring_peek(i);
        int written = snprintf(&dump_buf[pos],
                               sizeof(dump_buf) - pos,
                               "%02X%s",
                               byte,
                               ((i + 1U) < dump_count) ? " " : "");
        if (written <= 0) {
            break;
        }
        pos = (uint16_t) (pos + (uint16_t) written);
        if (pos >= sizeof(dump_buf)) {
            pos = (uint16_t) (sizeof(dump_buf) - 1U);
            break;
        }
    }
    dump_buf[pos] = '\0';

    LOG_W("Servo[%u] read timeout: adr=0x%02X len=%u timeout=%lu rx_count=%u rx=\"%s\"",
          (unsigned int) motor_id,
          (unsigned int) adr,
          (unsigned int) payload_len,
          (unsigned long) timeout_ms,
          (unsigned int) rx_count,
          dump_buf);
}

static fsp_err_t servo_prepare_joint_runtime_locked(uint8_t motor_id) {
    fsp_err_t err = servo_write_u8_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, SERVO_TORQUE_ON);
    if (err == FSP_SUCCESS) {
        err = servo_write_u8_locked(motor_id, SERVO_ADDR_LOCK_FLAG, SERVO_LOCK_OFF);
    }
    return err;
}

static fsp_err_t servo_write_target_locked(uint8_t motor_id, uint16_t pos_cmd, uint16_t time_ms) {
    uint8_t params[4] = {
        (uint8_t) ((pos_cmd >> 8) & 0xFFU),
        (uint8_t) (pos_cmd & 0xFFU),
        (uint8_t) ((time_ms >> 8) & 0xFFU),
        (uint8_t) (time_ms & 0xFFU)
    };
    return servo_write_raw_locked(motor_id, SERVO_ADDR_TARGET_POSITION, params, sizeof(params));
}

static fsp_err_t servo_lock_joint_current_locked(uint8_t motor_id) {
    if (!motor_id_is_joint(motor_id)) {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    fsp_err_t err = servo_write_u8_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, SERVO_TORQUE_ON);
    if (err == FSP_SUCCESS) {
        err = servo_write_target_locked(motor_id,
                                        (uint16_t) servo_rad_to_cmd(g_motors[motor_get_index(motor_id)].feedback.position),
                                        0U);
    }
    if (err == FSP_SUCCESS) {
        err = servo_write_u8_locked(motor_id, SERVO_ADDR_LOCK_FLAG, SERVO_LOCK_ON);
    }

    return err;
}

static fsp_err_t servo_prepare_gripper_locked(void) {
    fsp_err_t err = servo_write_u8_locked(MOTOR_ID_GRIPPER,
                                          SERVO_ADDR_TORQUE_SWITCH,
                                          SERVO_TORQUE_ON);
    if (err == FSP_SUCCESS) {
        err = servo_write_u8_locked(MOTOR_ID_GRIPPER,
                                    SERVO_ADDR_LOCK_FLAG,
                                    SERVO_LOCK_OFF);
    }
    return err;
}

static fsp_err_t servo_set_gripper_torque_limit_locked(uint16_t torque_limit) {
    uint16_t clamped = servo_clamp_u16((int32_t) torque_limit, 0U, SERVO_GRIPPER_LIMIT_MAX_MA);

    if (g_gripper_torque_limit_valid && (g_gripper_torque_limit_cache == clamped)) {
        return FSP_SUCCESS;
    }

    fsp_err_t err = servo_write_u16_locked(MOTOR_ID_GRIPPER,
                                           SERVO_ADDR_MAX_TORQUE,
                                           clamped);
    if (err == FSP_SUCCESS) {
        g_gripper_torque_limit_cache = clamped;
        g_gripper_torque_limit_valid = true;
    }

    return err;
}

static fsp_err_t servo_move_gripper_cmd_locked(uint16_t target_cmd, uint16_t torque_limit) {
    fsp_err_t err = servo_prepare_gripper_locked();
    if (err == FSP_SUCCESS) {
        err = servo_set_gripper_torque_limit_locked(torque_limit);
    }
    if (err == FSP_SUCCESS) {
        err = servo_write_target_locked(MOTOR_ID_GRIPPER,
                                        servo_gripper_clamp_target_cmd((int32_t) target_cmd),
                                        SERVO_GRIPPER_MOVE_TIME_MS);
    }
    return err;
}

static uint16_t servo_torque_limit_from_cmd(int16_t torque_cmd) {
    int32_t magnitude = (torque_cmd < 0) ? -(int32_t) torque_cmd : (int32_t) torque_cmd;
    return servo_clamp_u16(magnitude, 0U, SERVO_GRIPPER_LIMIT_MAX_MA);
}

static fsp_err_t servo_gripper_move(uint16_t target_cmd, int16_t torque_cmd) {
    fsp_err_t err = servo_take_lock(20U);
    uint16_t torque_limit = servo_torque_limit_from_cmd(torque_cmd);
    if (err != FSP_SUCCESS) {
        return err;
    }

    err = servo_move_gripper_cmd_locked(target_cmd, torque_limit);
    xSemaphoreGive(g_servo_mutex);
    return err;
}

static fsp_err_t servo_refresh_single_motor_locked(uint8_t motor_id, bool read_current, bool read_temp) {
    uint8_t pos_payload[2];
    fsp_err_t err = servo_read_data_locked(motor_id, SERVO_ADDR_CURRENT_POSITION, pos_payload, sizeof(pos_payload));
    if (err != FSP_SUCCESS) {
        return err;
    }

    uint8_t motor_index = motor_get_index(motor_id);
    if (motor_index >= MOTOR_NUM) {
        return FSP_ERR_NOT_FOUND;
    }

    uint16_t pos_cmd = (uint16_t) (((uint16_t) pos_payload[0] << 8) | (uint16_t) pos_payload[1]);
    float position_rad = servo_cmd_to_rad(pos_cmd);
    uint32_t now_tick = servo_now_ticks();
    float velocity_rad_s = 0.0f;

    if (g_position_fb_valid[motor_index]) {
        uint32_t delta_tick = now_tick - g_last_position_tick[motor_index];
        if (delta_tick > 0U) {
            float dt = (float) delta_tick / (float) configTICK_RATE_HZ;
            if (dt > 0.0f) {
                velocity_rad_s = (position_rad - g_last_position_fb[motor_index]) / dt;
            }
        }
    }

    g_last_position_fb[motor_index] = position_rad;
    g_last_position_tick[motor_index] = now_tick;
    g_position_fb_valid[motor_index] = true;
    uint8_t joint_index = 0U;
    if (servo_motor_is_active_joint(motor_id, &joint_index)) {
        g_last_joint_feedback_tick[joint_index] = now_tick;
    }

    g_motors[motor_index].feedback.position = position_rad;
    g_motors[motor_index].feedback.velocity = velocity_rad_s;

    if (read_current) {
        uint8_t cur_payload[2];
        servo_sensor_read_gap();
        fsp_err_t cur_err = servo_read_sensor_data_locked(motor_id,
                                                          SERVO_ADDR_CURRENT_CURRENT,
                                                          cur_payload,
                                                          sizeof(cur_payload));
        if (cur_err == FSP_SUCCESS) {
            uint16_t cur_raw = (uint16_t) (((uint16_t) cur_payload[0] << 8) | (uint16_t) cur_payload[1]);
            g_motors[motor_index].feedback.torque = ((float) cur_raw) * SERVO_TORQUE_PROXY_SCALE_F;
        }
    }

    if (read_temp) {
        uint8_t temp_payload[1];
        servo_sensor_read_gap();
        fsp_err_t temp_err = servo_read_sensor_data_locked(motor_id,
                                                           SERVO_ADDR_CURRENT_TEMP,
                                                           temp_payload,
                                                           sizeof(temp_payload));
        if (temp_err == FSP_SUCCESS) {
            g_motors[motor_index].feedback.temperature = (float) temp_payload[0];
        }
    }

    return FSP_SUCCESS;
}

static fsp_err_t servo_refresh_feedback_locked(servo_refresh_mode_t mode) {
    g_feedback_cycle++;

    bool refresh_full = (mode == SERVO_REFRESH_MODE_FULL);
    bool initial_sample = refresh_full && (g_last_feedback_tick == 0U);
    bool current_cycle = refresh_full &&
                         (initial_sample || ((g_feedback_cycle % SERVO_CURRENT_REFRESH_DIV) == 0U));
    bool temp_cycle = refresh_full &&
                      (initial_sample || ((g_feedback_cycle % SERVO_TEMP_REFRESH_DIV) == 0U));
    uint8_t current_index = g_current_refresh_index;
    uint8_t temp_index = g_temp_refresh_index;
    uint8_t motor_count = refresh_full ? MOTOR_ACTIVE_MOTOR_NUM : MOTOR_ACTIVE_JOINT_NUM;

    for (uint8_t i = 0U; i < motor_count; ++i) {
        bool read_current = refresh_full && (initial_sample || (current_cycle && (i == current_index)));
        bool read_temp = refresh_full && (initial_sample || (temp_cycle && (i == temp_index)));
        uint8_t motor_id = refresh_full ? k_all_motor_ids[i] : k_joint_ids[i];
        fsp_err_t err = servo_refresh_single_motor_locked(motor_id, read_current, read_temp);
        if (err != FSP_SUCCESS) {
            return err;
        }
    }

    if (refresh_full && !initial_sample && current_cycle) {
        g_current_refresh_index = (uint8_t) ((g_current_refresh_index + 1U) % MOTOR_ACTIVE_MOTOR_NUM);
    }

    if (refresh_full && !initial_sample && temp_cycle) {
        g_temp_refresh_index = (uint8_t) ((g_temp_refresh_index + 1U) % MOTOR_ACTIVE_MOTOR_NUM);
    }

    g_last_feedback_tick = servo_now_ticks();
    g_servo_connected = true;
    return FSP_SUCCESS;
}

static fsp_err_t servo_refresh_feedback_retry_locked(servo_refresh_mode_t mode) {
    fsp_err_t err = FSP_ERR_NOT_INITIALIZED;

    for (uint8_t attempt = 0U; attempt < SERVO_INIT_RETRY_COUNT; ++attempt) {
        err = servo_refresh_feedback_locked(mode);
        if (err == FSP_SUCCESS) {
            return FSP_SUCCESS;
        }

        if ((attempt + 1U) < SERVO_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_RETRY_DELAY_MS));
        }
    }

    return err;
}

static fsp_err_t servo_refresh_runtime(servo_refresh_mode_t mode) {
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) {
        return err;
    }

    err = servo_refresh_feedback_locked(mode);
    if (err != FSP_SUCCESS) {
        servo_ring_clear();
        err = servo_refresh_feedback_locked(mode);
    }

    xSemaphoreGive(g_servo_mutex);

    if (err == FSP_SUCCESS) {
        g_feedback_fail_count = 0U;
        g_servo_connected = true;
    } else if (g_feedback_fail_count < UINT8_MAX) {
        g_feedback_fail_count++;
    }

    return err;
}

/* -------------------------------------------------------------------------- */
/* 对外驱动接口                                                                */
/* -------------------------------------------------------------------------- */
bool servo_init(void) {
    fsp_err_t err = FSP_SUCCESS;

    if (g_servo_mutex == NULL) {
        g_servo_mutex = xSemaphoreCreateMutexStatic(&g_servo_mutex_memory);
    }

    if ((g_servo_mutex == NULL) || (servo_take_lock(100U) != FSP_SUCCESS)) {
        return false;
    }

    if (!g_servo_uart_open) {
        fsp_err_t open_err = g_uart_servo.p_api->open(g_uart_servo.p_ctrl, g_uart_servo.p_cfg);
        if (open_err == FSP_ERR_ALREADY_OPEN) {
            (void) g_uart_servo.p_api->close(g_uart_servo.p_ctrl);
            open_err = g_uart_servo.p_api->open(g_uart_servo.p_ctrl, g_uart_servo.p_cfg);
        }

        if (open_err != FSP_SUCCESS) {
            xSemaphoreGive(g_servo_mutex);
            return false;
        }

        g_servo_uart_open = true;
    }

    servo_ring_clear();
    memset(g_last_position_fb, 0, sizeof(g_last_position_fb));
    memset(g_last_position_tick, 0, sizeof(g_last_position_tick));
    memset(g_position_fb_valid, 0, sizeof(g_position_fb_valid));
    memset(g_last_joint_feedback_tick, 0, sizeof(g_last_joint_feedback_tick));
    g_last_feedback_tick = 0U;
    g_feedback_cycle = 0U;
    g_feedback_fail_count = 0U;
    g_servo_connected = false;
    g_current_refresh_index = 0U;
    g_temp_refresh_index = 0U;
    g_gripper_torque_limit_cache = 0U;
    g_gripper_torque_limit_valid = false;

    vTaskDelay(pdMS_TO_TICKS(SERVO_INIT_SETTLE_MS));

    bool ok = true;
    for (uint8_t i = 0U; i < MOTOR_ACTIVE_MOTOR_NUM; ++i) {
        err = servo_probe_motor_retry_locked(k_all_motor_ids[i]);
        if (err != FSP_SUCCESS) {
            LOG_E("Servo[%u] probe failed: %d", k_all_motor_ids[i], err);
            ok = false;
            break;
        }
    }

    if (ok) {
        for (uint8_t i = 0U; i < MOTOR_ACTIVE_JOINT_NUM; ++i) {
            err = servo_prepare_joint_runtime_locked(k_joint_ids[i]);
            if (err != FSP_SUCCESS) {
                LOG_E("Servo[%u] runtime prepare failed: %d", k_joint_ids[i], err);
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        g_gripper_torque_limit_cache = 0U;
        g_gripper_torque_limit_valid = false;
    }

    if (ok) {
        err = servo_refresh_feedback_retry_locked(SERVO_REFRESH_MODE_FULL);
        if (err != FSP_SUCCESS) {
            LOG_E("Initial servo feedback refresh failed: %d", err);
            ok = false;
        }
    }

    xSemaphoreGive(g_servo_mutex);

    if (!ok) {
        g_servo_connected = false;
        g_last_feedback_tick = 0U;
        g_feedback_fail_count = 0U;
        memset(g_last_joint_feedback_tick, 0, sizeof(g_last_joint_feedback_tick));
    }

    return ok;
}

void servo_deinit(void) {
    fsp_err_t err = servo_take_lock(100U);
    if (err != FSP_SUCCESS) {
        return;
    }

    if (g_servo_uart_open) {
        (void) g_uart_servo.p_api->close(g_uart_servo.p_ctrl);
        g_servo_uart_open = false;
    }

    g_servo_connected = false;
    g_feedback_fail_count = 0U;
    memset(g_last_joint_feedback_tick, 0, sizeof(g_last_joint_feedback_tick));
    servo_ring_clear();
    xSemaphoreGive(g_servo_mutex);
}

fsp_err_t servo_refresh_feedback(void) {
    return servo_refresh_runtime(SERVO_REFRESH_MODE_FULL);
}

fsp_err_t servo_refresh_joint_feedback(void) {
    return servo_refresh_runtime(SERVO_REFRESH_MODE_JOINTS_ONLY);
}

fsp_err_t servo_write_joint_positions(const float q_target[MOTOR_ACTIVE_JOINT_NUM]) {
    uint8_t params[1U + MOTOR_ACTIVE_JOINT_NUM * 5U];
    uint8_t frame[SERVO_FRAME_MAX_SIZE];
    uint8_t offset = 0U;

    if (q_target == NULL) return FSP_ERR_INVALID_POINTER;
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    params[offset++] = MOTOR_ACTIVE_JOINT_NUM;
    for (uint8_t i = 0U; i < MOTOR_ACTIVE_JOINT_NUM; ++i) {
        uint16_t pos_cmd = (uint16_t) servo_rad_to_cmd(q_target[i]);
        params[offset++] = k_joint_ids[i];
        params[offset++] = (uint8_t) ((pos_cmd >> 8) & 0xFFU);
        params[offset++] = (uint8_t) (pos_cmd & 0xFFU);
        params[offset++] = (uint8_t) ((SERVO_JOINT_MOVE_TIME_MS >> 8) & 0xFFU);
        params[offset++] = (uint8_t) (SERVO_JOINT_MOVE_TIME_MS & 0xFFU);
    }

    uint16_t frame_len = servo_build_frame(frame,
                                           SERVO_ID_BROADCAST,
                                           SERVO_CMD_MULTI_WRITE,
                                           SERVO_ADDR_TARGET_POSITION,
                                           params,
                                           offset);

    servo_ring_clear();
    err = servo_send_frame_locked(frame, frame_len);
    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_hold_joint_current(void) {
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    err = FSP_SUCCESS;
    for (uint8_t i = 0U; i < MOTOR_ACTIVE_JOINT_NUM; ++i) {
        err = servo_lock_joint_current_locked(k_joint_ids[i]);
        if (err != FSP_SUCCESS) {
            break;
        }
    }

    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_set_joint_servo_mode(uint8_t motor_id) {
    if (!motor_id_is_joint(motor_id)) return FSP_ERR_INVALID_ARGUMENT;
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    err = servo_prepare_joint_runtime_locked(motor_id);
    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_lock_joint_current(uint8_t motor_id) {
    if (!motor_id_is_joint(motor_id)) return FSP_ERR_INVALID_ARGUMENT;
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    err = servo_lock_joint_current_locked(motor_id);
    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_unlock_joint(uint8_t motor_id) {
    if (!motor_id_is_joint(motor_id)) return FSP_ERR_INVALID_ARGUMENT;
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    err = servo_write_u8_locked(motor_id, SERVO_ADDR_LOCK_FLAG, SERVO_LOCK_OFF);
    if (err == FSP_SUCCESS) {
        err = servo_write_u8_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, SERVO_TORQUE_OFF);
    }

    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_stop_motor(uint8_t motor_id, bool hold_position) {
    if (!motor_id_is_valid(motor_id)) return FSP_ERR_INVALID_ARGUMENT;
    fsp_err_t err = servo_take_lock(20U);
    if (err != FSP_SUCCESS) return err;

    if (motor_id == MOTOR_ID_GRIPPER) {
        err = servo_write_u8_locked(motor_id, SERVO_ADDR_LOCK_FLAG, SERVO_LOCK_OFF);
        if (err == FSP_SUCCESS) {
            err = servo_write_u8_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, hold_position ? SERVO_TORQUE_ON : SERVO_TORQUE_OFF);
        }
        if (err == FSP_SUCCESS && hold_position) {
            err = servo_write_target_locked(motor_id,
                                            (uint16_t) servo_rad_to_cmd(g_motors[motor_get_index(motor_id)].feedback.position),
                                            0U);
        }
    } else if (hold_position) {
        err = servo_lock_joint_current_locked(motor_id);
    } else {
        err = servo_write_u8_locked(motor_id, SERVO_ADDR_LOCK_FLAG, SERVO_LOCK_OFF);
        if (err == FSP_SUCCESS) {
            err = servo_write_u8_locked(motor_id, SERVO_ADDR_TORQUE_SWITCH, SERVO_TORQUE_OFF);
        }
    }

    xSemaphoreGive(g_servo_mutex);
    return err;
}

fsp_err_t servo_set_zero(uint8_t motor_id) {
    (void) motor_id;
    LOG_W("servo_set_zero is not supported by servo-bus-r.pdf standard protocol");
    return FSP_ERR_ABORTED;
}

bool servo_supports_zero_calibration(void) {
    return false;
}

fsp_err_t servo_gripper_grasp(int16_t torque_cmd) {
    return servo_gripper_move(SERVO_GRIPPER_CLOSED_CMD, torque_cmd);
}

fsp_err_t servo_gripper_hold(int16_t torque_cmd) {
    return servo_gripper_grasp(torque_cmd);
}

fsp_err_t servo_gripper_release(int16_t torque_cmd) {
    return servo_gripper_move(SERVO_GRIPPER_RELEASE_CMD, torque_cmd);
}

fsp_err_t servo_gripper_stop(void) {
    return FSP_SUCCESS;
}

uint16_t servo_gripper_feedback_cmd(void) {
    return servo_gripper_clamp_target_cmd((int32_t) servo_rad_to_cmd(
        g_motors[motor_get_index(MOTOR_ID_GRIPPER)].feedback.position));
}

fsp_err_t servo_gripper_move_to_cmd(int32_t target_cmd, uint16_t *applied_cmd) {
    fsp_err_t err = servo_take_lock(20U);
    uint16_t clamped_cmd = servo_gripper_clamp_target_cmd(target_cmd);
    uint16_t torque_limit = servo_get_gripper_current_limit_ma();
    if (err != FSP_SUCCESS) {
        return err;
    }

    err = servo_move_gripper_cmd_locked(clamped_cmd, torque_limit);
    xSemaphoreGive(g_servo_mutex);

    if ((err == FSP_SUCCESS) && (applied_cmd != NULL)) {
        *applied_cmd = clamped_cmd;
    }

    return err;
}

void servo_log_link_diagnostics(void) {
    fsp_err_t err = servo_take_lock(100U);
    if (err == FSP_ERR_NOT_INITIALIZED) {
        LOG_W("Servo diag skipped: mutex not initialized");
        return;
    }
    if (err != FSP_SUCCESS) {
        LOG_W("Servo diag skipped: bus busy");
        return;
    }

    LOG_W("Servo link diagnostics begin");
    for (uint8_t i = 0U; i < MOTOR_ACTIVE_MOTOR_NUM; ++i) {
        servo_log_motor_diag_locked(k_all_motor_ids[i]);
    }
    LOG_W("Servo link diagnostics end");

    xSemaphoreGive(g_servo_mutex);
}

void servo_link_check(void) {
    bool have_joint_feedback = false;

    for (uint8_t i = 0U; i < MOTOR_ACTIVE_JOINT_NUM; ++i) {
        if (g_last_joint_feedback_tick[i] != 0U) {
            have_joint_feedback = true;
            break;
        }
    }

    bool connected = have_joint_feedback &&
                     (g_feedback_fail_count < SERVO_LINK_FAIL_LIMIT);

    g_servo_connected = connected;
    motion_link_set(connected);
}

bool servo_is_connected(void) {
    return g_servo_connected;
}

void uart_servo_callback(uart_callback_args_t *p_args) {
    if (p_args == NULL) return;

    switch (p_args->event) {
        case UART_EVENT_RX_CHAR:
            servo_ring_push_isr((uint8_t) p_args->data);
            break;

        case UART_EVENT_TX_COMPLETE:
            g_uart_tx_done = true;
            break;

        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_PARITY:
            g_uart_error = true;
            break;

        default:
            break;
    }
}
