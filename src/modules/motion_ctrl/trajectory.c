/**
 * @file trajectory.c
 * @brief Natural cubic spline trajectory generator
 */

#include "trajectory.h"
#include <math.h>
#include <string.h>

static bool compute_natural_cubic_spline(const float x[],
                                         const float y[],
                                         uint8_t n,
                                         float a[],
                                         float b[],
                                         float c[],
                                         float d[]);
static void eval_cubic_poly(float t, float a, float b, float c, float d, float *q, float *v);
static float motion_frame_get_angle(const motion_frame_t *frame, uint8_t joint);

void traj_reset(traj_controller_t *traj) {
    if (traj == NULL) {
        return;
    }

    memset(traj, 0, sizeof(*traj));
    traj->state = TRAJ_IDLE;
}

bool traj_init_from_sequence(traj_controller_t *traj, const action_sequence_t *seq) {
    if ((traj == NULL) || (seq == NULL) || (seq->frame_count < 2U) || (seq->frame_count > MAX_FRAMES_PER_SEQ)) {
        return false;
    }

    traj_reset(traj);

    uint8_t point_count = (uint8_t) seq->frame_count;
    uint8_t segment_count = (uint8_t) (point_count - 1U);
    float time_points[MAX_FRAMES_PER_SEQ] = {0.0f};

    traj->frame_count = point_count;
    traj->total_segments = segment_count;
    memcpy(traj->frames, seq->frames, (size_t) point_count * sizeof(motion_frame_t));

    for (uint8_t i = 1U; i < point_count; ++i) {
        traj->total_duration += (float) seq->frames[i].duration_ms / 1000.0f;
        time_points[i] = traj->total_duration;
    }

    for (uint8_t joint = 0U; joint < TRAJ_MAX_JOINTS; ++joint) {
        float angles[MAX_FRAMES_PER_SEQ] = {0.0f};
        float a[MAX_FRAMES_PER_SEQ] = {0.0f};
        float b[MAX_FRAMES_PER_SEQ] = {0.0f};
        float c[MAX_FRAMES_PER_SEQ] = {0.0f};
        float d[MAX_FRAMES_PER_SEQ] = {0.0f};

        for (uint8_t i = 0U; i < point_count; ++i) {
            angles[i] = motion_frame_get_angle(&seq->frames[i], joint);
        }

        if (!compute_natural_cubic_spline(time_points, angles, point_count, a, b, c, d)) {
            traj->state = TRAJ_ERROR;
            return false;
        }

        for (uint8_t seg = 0U; seg < segment_count; ++seg) {
            traj->segments[seg][joint].a[0] = a[seg];
            traj->segments[seg][joint].a[1] = b[seg];
            traj->segments[seg][joint].a[2] = c[seg];
            traj->segments[seg][joint].a[3] = d[seg];
            traj->segments[seg][joint].duration = (float) seq->frames[seg + 1U].duration_ms / 1000.0f;
            traj->segments[seg][joint].action = seq->frames[seg + 1U].action;
        }
    }

    traj->state = TRAJ_RUNNING;
    return true;
}

static bool compute_natural_cubic_spline(const float x[],
                                         const float y[],
                                         uint8_t n,
                                         float a[],
                                         float b[],
                                         float c[],
                                         float d[]) {
    if (n < 2U) {
        return false;
    }

    uint8_t point_count = n;
    uint8_t segment_count = (uint8_t) (point_count - 1U);
    float h[MAX_FRAMES_PER_SEQ - 1] = {0.0f};
    float alpha[MAX_FRAMES_PER_SEQ] = {0.0f};
    float l[MAX_FRAMES_PER_SEQ] = {0.0f};
    float mu[MAX_FRAMES_PER_SEQ] = {0.0f};
    float z[MAX_FRAMES_PER_SEQ] = {0.0f};
    float c_spline[MAX_FRAMES_PER_SEQ] = {0.0f};

    for (uint8_t i = 0U; i < segment_count; ++i) {
        h[i] = x[i + 1U] - x[i];
        if (h[i] <= 0.0f) {
            return false;
        }
    }

    for (uint8_t i = 1U; i < segment_count; ++i) {
        alpha[i] = 3.0f / h[i] * (y[i + 1U] - y[i]) - 3.0f / h[i - 1U] * (y[i] - y[i - 1U]);
    }

    l[0] = 1.0f;
    for (uint8_t i = 1U; i < segment_count; ++i) {
        l[i] = 2.0f * (x[i + 1U] - x[i - 1U]) - h[i - 1U] * mu[i - 1U];
        if (fabsf(l[i]) < 1e-6f) {
            return false;
        }
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1U] * z[i - 1U]) / l[i];
    }

    l[segment_count] = 1.0f;
    for (int8_t i = (int8_t) segment_count - 1; i >= 0; --i) {
        c_spline[i] = z[i] - mu[i] * c_spline[i + 1];
    }

    for (uint8_t i = 0U; i < segment_count; ++i) {
        a[i] = y[i];
        b[i] = (y[i + 1U] - y[i]) / h[i] - h[i] * (c_spline[i + 1U] + 2.0f * c_spline[i]) / 3.0f;
        c[i] = c_spline[i];
        d[i] = (c_spline[i + 1U] - c_spline[i]) / (3.0f * h[i]);
    }

    return true;
}

static void eval_cubic_poly(float t, float a, float b, float c, float d, float *q, float *v) {
    if (q != NULL) {
        *q = a + t * (b + t * (c + t * d));
    }
    if (v != NULL) {
        *v = b + t * (2.0f * c + t * 3.0f * d);
    }
}

void traj_eval(traj_controller_t *traj,
               float t,
               float q_out[TRAJ_MAX_JOINTS],
               float v_out[TRAJ_MAX_JOINTS],
               bool *seg_done,
               bool *seq_done) {
    if ((traj == NULL) || (traj->state != TRAJ_RUNNING)) {
        if (q_out != NULL) {
            memset(q_out, 0, TRAJ_MAX_JOINTS * sizeof(float));
        }
        if (v_out != NULL) {
            memset(v_out, 0, TRAJ_MAX_JOINTS * sizeof(float));
        }
        if (seg_done != NULL) {
            *seg_done = false;
        }
        if (seq_done != NULL) {
            *seq_done = true;
        }
        return;
    }

    if (seg_done != NULL) {
        *seg_done = false;
    }
    if (seq_done != NULL) {
        *seq_done = false;
    }

    if (t >= traj->total_duration) {
        uint8_t last_frame = (uint8_t) (traj->frame_count - 1U);
        if (q_out != NULL) {
            for (uint8_t joint = 0U; joint < TRAJ_MAX_JOINTS; ++joint) {
                q_out[joint] = motion_frame_get_angle(&traj->frames[last_frame], joint);
            }
        }
        if (v_out != NULL) {
            memset(v_out, 0, TRAJ_MAX_JOINTS * sizeof(float));
        }
        if (seq_done != NULL) {
            *seq_done = true;
        }
        return;
    }

    uint8_t segment = 0U;
    float segment_start_time = 0.0f;
    float segment_end_time = 0.0f;

    for (uint8_t i = 0U; i < traj->total_segments; ++i) {
        segment_end_time += traj->segments[i][0].duration;
        if (t < segment_end_time) {
            segment = i;
            break;
        }
        segment_start_time = segment_end_time;
    }

    float segment_time = t - segment_start_time;
    traj->current_segment = segment;

    if (seg_done != NULL) {
        float time_remaining = traj->segments[segment][0].duration - segment_time;
        *seg_done = (time_remaining < 0.001f);
    }

    for (uint8_t joint = 0U; joint < TRAJ_MAX_JOINTS; ++joint) {
        traj_segment_t *seg = &traj->segments[segment][joint];
        eval_cubic_poly(segment_time, seg->a[0], seg->a[1], seg->a[2], seg->a[3], &q_out[joint], &v_out[joint]);
    }
}

bool traj_step(traj_controller_t *traj,
               float dt,
               float q_out[TRAJ_MAX_JOINTS],
               float v_out[TRAJ_MAX_JOINTS]) {
    if ((traj == NULL) || (traj->state != TRAJ_RUNNING)) {
        return false;
    }

    traj->elapsed_time += dt;

    if (traj->elapsed_time >= traj->total_duration) {
        uint8_t last_frame = (uint8_t) (traj->frame_count - 1U);
        traj->state = TRAJ_COMPLETED;

        if (q_out != NULL) {
            for (uint8_t joint = 0U; joint < TRAJ_MAX_JOINTS; ++joint) {
                q_out[joint] = motion_frame_get_angle(&traj->frames[last_frame], joint);
            }
        }
        if (v_out != NULL) {
            memset(v_out, 0, TRAJ_MAX_JOINTS * sizeof(float));
        }
        return false;
    }

    bool seg_done = false;
    bool seq_done = false;
    traj_eval(traj, traj->elapsed_time, q_out, v_out, &seg_done, &seq_done);

    if (seg_done) {
        traj->current_segment++;
    }

    return true;
}

traj_state_t traj_get_state(const traj_controller_t *traj) {
    return (traj != NULL) ? traj->state : TRAJ_ERROR;
}

static float motion_frame_get_angle(const motion_frame_t *frame, uint8_t joint) {
    if (frame == NULL) {
        return 0.0f;
    }

    switch (joint) {
        case 0U:
            return frame->angle_m1;
        case 1U:
            return frame->angle_m2;
        case 2U:
            return frame->angle_m3;
        case 3U:
            return frame->angle_m4;
        case 4U:
            return frame->angle_m5;
        default:
            return 0.0f;
    }
}
