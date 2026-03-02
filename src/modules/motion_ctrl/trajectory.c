/**
 * @file trajectory.c
 * @brief 自然三次样条轨迹生成器实现
 * @date 2026-02-27
 * @author Ma Ziteng
 */

#include "trajectory.h"
#include <string.h>
#include <math.h>

/* 内部函数声明 */
static bool compute_natural_cubic_spline(const float x[], const float y[], uint8_t n,
                                         float a[], float b[], float c[], float d[]);
static void eval_cubic_poly(float t, float a, float b, float c, float d,
                           float *q, float *v);

/**
 * @brief 重置轨迹控制器
 */
void traj_reset(traj_controller_t *traj)
{
    if (traj == NULL) {
        return;
    }
    
    memset(traj, 0, sizeof(traj_controller_t));
    traj->state = TRAJ_IDLE;
}

/**
 * @brief 从动作序列初始化轨迹
 */
bool traj_init_from_sequence(traj_controller_t *traj, const action_sequence_t *seq) {
    if (traj == NULL || seq == NULL || seq->frame_count < 2 || seq->frame_count > MAX_FRAMES_PER_SEQ) {
        return false;
    }
    
    /* 重置控制器 */
    traj_reset(traj);
    
    uint8_t point_count = (uint8_t)seq->frame_count;
    uint8_t segment_count = point_count - 1U;

    /* 保存原始帧数据 */
    traj->frame_count = point_count;
    traj->total_segments = segment_count;
    memcpy(traj->frames, seq->frames, point_count * sizeof(motion_frame_t));
    
    /* 计算总持续时间和时间戳数组
     * 约定：frames[i].duration_ms 表示“到达第 i 帧所需时长”（i>=1）
     */
    float time_points[MAX_FRAMES_PER_SEQ] = {0};
    traj->total_duration = 0.0f;

    /* 第一帧时间为0，后续帧时间由“该帧duration”累加得到 */
    time_points[0] = 0.0f;
    for (uint8_t i = 1; i < point_count; i++) {
        traj->total_duration += (float)seq->frames[i].duration_ms / 1000.0f; /* ms转秒 */
        time_points[i] = traj->total_duration;
    }
    
    /* 为每个关节计算自然三次样条系数 */
    for (uint8_t joint = 0; joint < TRAJ_MAX_JOINTS; joint++) {
        /* 提取该关节在所有关键帧的角度 */
        float angles[MAX_FRAMES_PER_SEQ];
        for (uint8_t i = 0; i < point_count; i++) {
            switch (joint) {
                case 0: angles[i] = seq->frames[i].angle_m1; break;
                case 1: angles[i] = seq->frames[i].angle_m2; break;
                case 2: angles[i] = seq->frames[i].angle_m3; break;
                case 3: angles[i] = seq->frames[i].angle_m4; break;
                default: angles[i] = 0.0f; break;
            }
        }
        
        /* 计算自然三次样条系数 */
        float a[MAX_FRAMES_PER_SEQ] = {0};
        float b[MAX_FRAMES_PER_SEQ] = {0};
        float c[MAX_FRAMES_PER_SEQ] = {0};
        float d[MAX_FRAMES_PER_SEQ] = {0};
        
        if (!compute_natural_cubic_spline(time_points, angles, point_count,
                                          a, b, c, d)) {
            traj->state = TRAJ_ERROR;
            return false;
        }
        
        /* 保存每段的系数 */
        for (uint8_t seg = 0; seg < segment_count; seg++) {
            traj->segments[seg][joint].a[0] = a[seg];  /* 常数项 */
            traj->segments[seg][joint].a[1] = b[seg];  /* 一次项系数 */
            traj->segments[seg][joint].a[2] = c[seg];  /* 二次项系数 */
            traj->segments[seg][joint].a[3] = d[seg];  /* 三次项系数 */
            
            /* 保存段持续时间（到达下一关键帧的持续时间） */
            float seg_duration = (float)seq->frames[seg + 1U].duration_ms / 1000.0f;
            traj->segments[seg][joint].duration = seg_duration;

            /* 保存动作（下一关键帧的动作） */
            traj->segments[seg][joint].action = seq->frames[seg + 1U].action;
        }
    }
    
    traj->state = TRAJ_RUNNING;
    return true;
}

/**
 * @brief 计算自然三次样条系数
 * @param x 时间点数组
 * @param y 角度数组
 * @param n 数据点数量
 * @param a,b,c,d 输出系数数组（长度n-1）
 * @return true 成功，false 失败
 * 
 * 算法：自然三次样条，边界条件为二阶导数为0
 * 使用三对角矩阵算法（Thomas算法）求解
 */
static bool compute_natural_cubic_spline(const float x[], const float y[], uint8_t n,
                                         float a[], float b[], float c[], float d[]) {
    if (n < 2) {
        return false;
    }

    uint8_t point_count = n;
    uint8_t segment_count = point_count - 1U;
    
    /* 计算时间间隔 h[i] = x[i+1] - x[i] */
    float h[MAX_FRAMES_PER_SEQ - 1] = {0};
    for (uint8_t i = 0; i < segment_count; i++) {
        h[i] = x[i+1] - x[i];
        if (h[i] <= 0.0f) {
            return false;  /* 时间必须递增 */
        }
    }
    
    /* 计算alpha数组 */
    float alpha[MAX_FRAMES_PER_SEQ] = {0};
    for (uint8_t i = 1; i < segment_count; i++) {
        alpha[i] = 3.0f/h[i] * (y[i+1] - y[i]) - 3.0f/h[i-1] * (y[i] - y[i-1]);
    }
    
    /* 三对角矩阵：l, mu, z 数组 */
    float l[MAX_FRAMES_PER_SEQ] = {0};
    float mu[MAX_FRAMES_PER_SEQ] = {0};
    float z[MAX_FRAMES_PER_SEQ] = {0};
    
    /* 自然边界条件：l[0] = 1, mu[0] = 0, z[0] = 0 */
    l[0] = 1.0f;
    mu[0] = 0.0f;
    z[0] = 0.0f;
    
    /* 前向消元 */
    for (uint8_t i = 1; i < segment_count; i++) {
        l[i] = 2.0f * (x[i+1] - x[i-1]) - h[i-1] * mu[i-1];
        if (fabsf(l[i]) < 1e-6f) {
            return false;  /* 奇异矩阵 */
        }
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i-1] * z[i-1]) / l[i];
    }
    
    /* 自然边界条件：l[n-1] = 1, z[n-1] = 0 */
    l[segment_count] = 1.0f;
    z[segment_count] = 0.0f;
    float c_spline[MAX_FRAMES_PER_SEQ] = {0};
    
    /* 后向代入计算c系数 */
    for (int8_t i = (int8_t)segment_count - 1; i >= 0; i--) {
        c_spline[i] = z[i] - mu[i] * c_spline[i+1];
    }
    
    /* 计算a,b,d系数 */
    for (uint8_t i = 0; i < segment_count; i++) {
        a[i] = y[i];
        b[i] = (y[i+1] - y[i]) / h[i] - h[i] * (c_spline[i+1] + 2.0f * c_spline[i]) / 3.0f;
        c[i] = c_spline[i];
        d[i] = (c_spline[i+1] - c_spline[i]) / (3.0f * h[i]);
    }
    
    return true;
}

/**
 * @brief 计算三次多项式及其导数
 * @param t 归一化时间（从段开始的时间，0 <= t <= duration）
 */
static void eval_cubic_poly(float t, float a, float b, float c, float d,
                           float *q, float *v) {
    if (q != NULL) 
        *q = a + t * (b + t * (c + t * d)); 
    if (v != NULL) 
        *v = b + t * (2.0f * c + t * 3.0f * d);
}

/**
 * @brief 轨迹求值
 */
void traj_eval(traj_controller_t *traj, float t, float q_out[4], float v_out[4],
               bool *seg_done, bool *seq_done) {
    if (traj == NULL || traj->state != TRAJ_RUNNING) {
        /* 返回默认值 */
        if (q_out != NULL) memset(q_out, 0, 4 * sizeof(float));
        if (v_out != NULL) memset(v_out, 0, 4 * sizeof(float));
        if (seg_done != NULL) *seg_done = false;
        if (seq_done != NULL) *seq_done = true;
        return;
    }
    
    /* 初始化输出 */
    if (seg_done != NULL) *seg_done = false;
    if (seq_done != NULL) *seq_done = false;
    
    /* 如果时间超过总持续时间，返回最后一个关键帧的值 */
    if (t >= traj->total_duration) {
        uint8_t last_frame = traj->frame_count - 1;
        if (q_out != NULL) {
            q_out[0] = traj->frames[last_frame].angle_m1;
            q_out[1] = traj->frames[last_frame].angle_m2;
            q_out[2] = traj->frames[last_frame].angle_m3;
            q_out[3] = traj->frames[last_frame].angle_m4;
        }
        if (v_out != NULL) {
            v_out[0] = 0.0f;
            v_out[1] = 0.0f;
            v_out[2] = 0.0f;
            v_out[3] = 0.0f;
        }
        if (seq_done != NULL) *seq_done = true;
        return;
    }
    
    /* 查找当前段 */
    uint8_t segment = 0;
    float segment_start_time = 0.0f;
    float segment_end_time = 0.0f;
    
    for (uint8_t i = 0; i < traj->total_segments; i++) {
        segment_end_time += traj->segments[i][0].duration;
        if (t < segment_end_time) {
            segment = i;
            break;
        }
        segment_start_time = segment_end_time;
    }
    
    /* 计算段内时间 */
    float segment_time = t - segment_start_time;

    /* 记录当前段 */
    traj->current_segment = segment;
    
    /* 检查是否接近段末尾（用于seg_done判断） */
    if (seg_done != NULL) {
        float time_remaining = traj->segments[segment][0].duration - segment_time;
        *seg_done = (time_remaining < 0.001f);  /* 1ms阈值 */
    }
    
    /* 计算每个关节的位置和速度 */
    for (uint8_t joint = 0; joint < TRAJ_MAX_JOINTS; joint++) {
        traj_segment_t *seg = &traj->segments[segment][joint];
        float q = 0.0f, v = 0.0f;
        
        eval_cubic_poly(segment_time, seg->a[0], seg->a[1], seg->a[2], seg->a[3], &q, &v);
        
        if (q_out != NULL) q_out[joint] = q;
        if (v_out != NULL) v_out[joint] = v;
    }
}

/**
 * @brief 步进轨迹控制器
 */
bool traj_step(traj_controller_t *traj, float dt, float q_out[4], float v_out[4]) {
    if (traj == NULL || traj->state != TRAJ_RUNNING) {
        return false;
    }
    
    /* 更新时间 */
    traj->elapsed_time += dt;
    
    /* 检查是否完成 */
    if (traj->elapsed_time >= traj->total_duration) {
        traj->state = TRAJ_COMPLETED;
        
        /* 返回最后一个关键帧的值 */
        uint8_t last_frame = traj->frame_count - 1;
        if (q_out != NULL) {
            q_out[0] = traj->frames[last_frame].angle_m1;
            q_out[1] = traj->frames[last_frame].angle_m2;
            q_out[2] = traj->frames[last_frame].angle_m3;
            q_out[3] = traj->frames[last_frame].angle_m4;
        }
        if (v_out != NULL) {
            memset(v_out, 0, 4 * sizeof(float));
        }
        return false;
    }
    
    /* 求值 */
    bool seg_done = false, seq_done = false;
    traj_eval(traj, traj->elapsed_time, q_out, v_out, &seg_done, &seq_done);
    
    /* 如果段完成，记录当前段索引（用于调试或状态报告） */
    if (seg_done) {
        traj->current_segment++;
    }
    
    return true;
}

/**
 * @brief 获取轨迹状态
 */
traj_state_t traj_get_state(const traj_controller_t *traj) {
    return (traj != NULL) ? traj->state : TRAJ_ERROR;
}