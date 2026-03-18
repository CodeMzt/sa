/**
 * @file gravity_comp.c
 * @brief 重力补偿模块实现
 * @date 2026-02-27
 * @author Ma Ziteng
 */

#include "gravity_comp.h"
#include <math.h>

/* 默认参数（适用于典型机械臂） */
static const float DEFAULT_SIN2_COEFF = 0.0f;   /* sin(q2) 项系数 */
static const float DEFAULT_COS2_COEFF = 0.0f;   /* cos(q2) 项系数 */
static const float DEFAULT_SIN23_COEFF = 0.0f;  /* sin(q2+q3) 项系数 */
static const float DEFAULT_COS23_COEFF = 0.00f; /* cos(q2+q3) 项系数 */
static const float DEFAULT_SIN234_COEFF = 0.0f; /* sin(q2+q3+q4) 项系数 */
static const float DEFAULT_COS234_COEFF = 0.00f;/* cos(q2+q3+q4) 项系数 */

void grav_init_default(grav_param_t *gp) {
    if (gp == NULL) return;
    
    gp->sin2_coeff = DEFAULT_SIN2_COEFF;
    gp->cos2_coeff = DEFAULT_COS2_COEFF;
    gp->sin23_coeff = DEFAULT_SIN23_COEFF;
    gp->cos23_coeff = DEFAULT_COS23_COEFF;
    gp->sin234_coeff = DEFAULT_SIN234_COEFF;
    gp->cos234_coeff = DEFAULT_COS234_COEFF;
}

void grav_compute(const grav_param_t *gp, const float q[4], float tau_ff_out[4]) {
    if (gp == NULL || q == NULL || tau_ff_out == NULL) {
        /* 参数无效时清零输出 */
        for (int i = 0; i < 4; i++) tau_ff_out[i] = 0.0f;
        return;
    }
    
    /* 计算所有必要的正弦和余弦值 */
    float s2 = sinf(q[1]), c2 = cosf(q[1]);
    float s23 = sinf(q[1] + q[2]), c23 = cosf(q[1] + q[2]);
    float s234 = sinf(q[1] + q[2] + q[3]), c234 = cosf(q[1] + q[2] + q[3]);
    
    tau_ff_out[0] = 0.0f;
    tau_ff_out[1] = gp->sin2_coeff * s2 + gp->cos2_coeff * c2 + 
                    gp->sin23_coeff * s23 + gp->cos23_coeff * c23 + 
                    gp->sin234_coeff * s234 + gp->cos234_coeff * c234;
    tau_ff_out[2] = gp->sin23_coeff * s23 + gp->cos23_coeff * c23 + 
                    gp->sin234_coeff * s234 + gp->cos234_coeff * c234;
    tau_ff_out[3] = gp->sin234_coeff * s234 + gp->cos234_coeff * c234;
}

void grav_set_params(grav_param_t *gp, float sin2_coeff, float cos2_coeff, 
                     float sin23_coeff, float cos23_coeff, 
                     float sin234_coeff, float cos234_coeff) {
    if (gp == NULL) return;
    
    gp->sin2_coeff = sin2_coeff;
    gp->cos2_coeff = cos2_coeff;
    gp->sin23_coeff = sin23_coeff;
    gp->cos23_coeff = cos23_coeff;
    gp->sin234_coeff = sin234_coeff;
    gp->cos234_coeff = cos234_coeff;
}