/**
 * @file gravity_comp.h
 * @brief 重力补偿模块（暂时移除，后续根据需要重新集成）
 * @date 2026-02-27
 * @author Ma Ziteng
 * 
 * 重力补偿计算接口，为4关节机械臂提供简化重力模型
 * 用于抵消重力对关节2~4的影响
 */

#ifndef GRAVITY_COMP_H_
#define GRAVITY_COMP_H_

#include <stdint.h>
#include <stdbool.h>

/* 重力补偿参数结构体 */
typedef struct {
    float sin2_coeff;    /* sin(q2) 项系数 */
    float cos2_coeff;    /* cos(q2) 项系数 */
    float sin23_coeff;   /* sin(q2+q3) 项系数 */
    float cos23_coeff;   /* cos(q2+q3) 项系数 */
    float sin234_coeff;  /* sin(q2+q3+q4) 项系数 */
    float cos234_coeff;  /* cos(q2+q3+q4) 项系数 */
} grav_param_t;

/**
 * @brief 初始化默认重力补偿参数
 * @param gp 参数结构体指针
 * @note 默认参数适用于典型机械臂，后续可通过最小二乘辨识优化
 */
void grav_init_default(grav_param_t *gp);

/**
 * @brief 计算重力补偿力矩
 * @param gp 参数结构体指针
 * @param q 4个关节角度数组（rad），[q1, q2, q3, q4]
 * @param tau_ff_out 输出力矩数组（Nm），[tau1, tau2, tau3, tau4]
 * @note 关节1力矩为0，关节2~4使用简化模型：
 *       tau2 = sin2_coeff*sin(q2) + cos2_coeff*cos(q2) + 
 *              sin23_coeff*sin(q2+q3) + cos23_coeff*cos(q2+q3) + 
 *              sin234_coeff*sin(q2+q3+q4) + cos234_coeff*cos(q2+q3+q4)
 *       tau3 = sin23_coeff*sin(q2+q3) + cos23_coeff*cos(q2+q3) + 
 *              sin234_coeff*sin(q2+q3+q4) + cos234_coeff*cos(q2+q3+q4)
 *       tau4 = sin234_coeff*sin(q2+q3+q4) + cos234_coeff*cos(q2+q3+q4)
 */
void grav_compute(const grav_param_t *gp, const float q[4], float tau_ff_out[4]);

/**
 * @brief 设置重力补偿参数
 * @param gp 参数结构体指针
 * @param sin2_coeff sin(q2) 项系数
 * @param cos2_coeff cos(q2) 项系数
 * @param sin23_coeff sin(q2+q3) 项系数
 * @param cos23_coeff cos(q2+q3) 项系数
 * @param sin234_coeff sin(q2+q3+q4) 项系数
 * @param cos234_coeff cos(q2+q3+q4) 项系数
 */
void grav_set_params(grav_param_t *gp, float sin2_coeff, float cos2_coeff, 
                     float sin23_coeff, float cos23_coeff, 
                     float sin234_coeff, float cos234_coeff);

#endif /* GRAVITY_COMP_H_ */