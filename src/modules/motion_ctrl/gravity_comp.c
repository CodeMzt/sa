/**
 * @file gravity_comp.c
 * @brief 重力补偿模块实现
 * @date 2026-02-27
 * @author Ma Ziteng
 */

#include "gravity_comp.h"
#include <math.h>

/* 默认参数（适用于典型机械臂） */
static const float DEFAULT_A = 0.8f;  /* sin(q2) 项系数 */
static const float DEFAULT_B = 0.6f;  /* sin(q2+q3) 项系数 */
static const float DEFAULT_C = 0.3f;  /* sin(q2+q3+q4) 项系数 */

void grav_init_default(grav_param_t *gp) {
    if (gp == NULL) {
        return;
    }
    
    gp->A = DEFAULT_A;
    gp->B = DEFAULT_B;
    gp->C = DEFAULT_C;
}

void grav_compute(const grav_param_t *gp, const float q[4], float tau_ff_out[4]) {
    if (gp == NULL || q == NULL || tau_ff_out == NULL) {
        /* 参数无效时清零输出 */
        for (int i = 0; i < 4; i++) tau_ff_out[i] = 0.0f;
        return;
    }
    float sin_q2 = sinf(q[1]);
    float sin_q23 = sinf(q[1] + q[2]);
    float sin_q234 = sinf(q[1] + q[2] + q[3]);
    
    tau_ff_out[0] = 0.0f;
    tau_ff_out[1] = gp->A * sin_q2 + gp->B * sin_q23 + gp->C * sin_q234;
    tau_ff_out[2] = gp->B * sin_q23 + gp->C * sin_q234;
    tau_ff_out[3] = gp->C * sin_q234;
}

void grav_set_params(grav_param_t *gp, float A, float B, float C) {
    if (gp == NULL) {
        return;
    }
    
    gp->A = A;
    gp->B = B;
    gp->C = C;
}