/**
 * @file drv_touch.c
 * @brief 触觉模块驱动实现
 * @author Ma Ziteng
 */

#include "drv_touch.h"
#include "servo_bus.h"

#define TOUCH_DATA_REG               (0x03U)
#define TOUCH_RAW_DATA_SIZE          (6U)
#define TOUCH_I2C_TX_WAIT_MS         (100U)
#define TOUCH_I2C_RX_WAIT_MS         (100U)

#define TOUCH_I2C_FLAG_TX_COMPLETE   (0x01U)
#define TOUCH_I2C_FLAG_RX_COMPLETE   (0x02U)
#define TOUCH_I2C_FLAG_ABORTED       (0x80U)

/**
 * @brief 触觉传感器最近一次采样结果
 *
 * 硬件返回顺序为 fx、fy、fz，每个轴占 2 字节，且低字节先行。
 */
g_touch_data_t g_touch_data_s = {0};

/**
 * @brief I2C 传输事件标志
 *
 * 该标志由中断回调更新，因此需要声明为 volatile。
 */
static volatile uint8_t g_touch_i2c_flag = 0U;
static volatile bool g_touch_ready = false;

/**
 * @brief 将小端字节序数据解析为 int16_t
 * @param data 指向低字节的起始地址
 * @retval 解析后的 16 位有符号数据
 */
static int16_t touch_decode_le_i16(const uint8_t *data){ return (int16_t) ((uint16_t) data[0] | ((uint16_t) data[1] << 8));}

/**
 * @brief 等待指定 I2C 事件完成
 * @param wait_flag 期望等待的事件标志
 * @param timeout_ms 超时时间，单位 ms
 * @retval FSP_SUCCESS      等待成功
 * @retval FSP_ERR_TIMEOUT  等待超时
 * @retval FSP_ERR_ABORTED  总线事务被中止
 */
static fsp_err_t touch_wait_i2c_flag(uint8_t wait_flag, uint32_t timeout_ms)
{
    uint32_t tick = timeout_ms;

    while (((g_touch_i2c_flag & (uint8_t) (wait_flag | TOUCH_I2C_FLAG_ABORTED)) == 0U) && (tick > 0U)){
        vTaskDelay(pdMS_TO_TICKS(1));
        tick--;
    }

    if ((g_touch_i2c_flag & TOUCH_I2C_FLAG_ABORTED) != 0U){
        g_touch_i2c_flag = 0U;
        return FSP_ERR_ABORTED;
    }

    if ((g_touch_i2c_flag & wait_flag) == 0U)return FSP_ERR_TIMEOUT;
    
    g_touch_i2c_flag &= (uint8_t) (~wait_flag);
    return FSP_SUCCESS;
}

void sci_i2c4_master_callback(i2c_master_callback_args_t *p_args){
    if (p_args == NULL)return;
    switch (p_args->event){
        case I2C_MASTER_EVENT_RX_COMPLETE:
            g_touch_i2c_flag |= TOUCH_I2C_FLAG_RX_COMPLETE;
            break;
        case I2C_MASTER_EVENT_TX_COMPLETE:
            g_touch_i2c_flag |= TOUCH_I2C_FLAG_TX_COMPLETE;
            break;
        case I2C_MASTER_EVENT_ABORTED:
            g_touch_i2c_flag |= TOUCH_I2C_FLAG_ABORTED;
            break;
        default:
            break;
    }
}

/**
 * @brief 初始化触觉模块 I2C 驱动
 * @retval FSP_SUCCESS 初始化成功
 * @retval 其他        初始化失败
 */
fsp_err_t touch_drv_init(void){
    fsp_err_t err = g_i2c_touch.p_api->open(g_i2c_touch.p_ctrl, g_i2c_touch.p_cfg);

    if ((err != FSP_SUCCESS) && (err != FSP_ERR_ALREADY_OPEN))
    {
        g_touch_ready = false;
        return err;
    }

    g_touch_i2c_flag = 0U;
    err = get_touch_data_process();
    if (err != FSP_SUCCESS) {
        g_touch_ready = false;
        return err;
    }

    g_touch_ready = true;
    return FSP_SUCCESS;
}

bool touch_drv_is_ready(void) {
    return g_touch_ready;
}

/**
 * @brief 读取一次触觉模块数据并刷新全局缓存
 * @retval FSP_SUCCESS      读取成功
 * @retval FSP_ERR_TIMEOUT  I2C 等待超时
 * @retval FSP_ERR_ABORTED  I2C 事务中止
 * @retval 其他             底层驱动返回的错误码
 */
fsp_err_t get_touch_data_process(void){
    fsp_err_t err = FSP_SUCCESS;
    uint8_t cmd = TOUCH_DATA_REG;
    uint8_t raw_data[TOUCH_RAW_DATA_SIZE] = {0};
    
    g_touch_i2c_flag = 0U;

    err = g_i2c_touch.p_api->write(g_i2c_touch.p_ctrl, &cmd, 1U, true);
    if (err != FSP_SUCCESS)return err;
    err = touch_wait_i2c_flag(TOUCH_I2C_FLAG_TX_COMPLETE, TOUCH_I2C_TX_WAIT_MS);
    if (err != FSP_SUCCESS)return err;

    err = g_i2c_touch.p_api->read(g_i2c_touch.p_ctrl, raw_data, TOUCH_RAW_DATA_SIZE, false);
    if (err != FSP_SUCCESS)return err;
    err = touch_wait_i2c_flag(TOUCH_I2C_FLAG_RX_COMPLETE, TOUCH_I2C_RX_WAIT_MS);
    if (err != FSP_SUCCESS)return err;

    g_touch_data_s.fx = touch_decode_le_i16(&raw_data[0]);
    g_touch_data_s.fy = touch_decode_le_i16(&raw_data[2]);
    g_touch_data_s.fz = touch_decode_le_i16(&raw_data[4]);

    return FSP_SUCCESS;
}
