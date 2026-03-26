/**
 * @file trajectory.h
 * @brief Natural cubic spline trajectory generator
 */

#ifndef TRAJECTORY_H_
#define TRAJECTORY_H_

#include "nvm_types.h"
#include <stdbool.h>
#include <stdint.h>

#define TRAJ_MAX_JOINTS MOTION_JOINT_COUNT

typedef enum {
    TRAJ_IDLE = 0,
    TRAJ_RUNNING,
    TRAJ_COMPLETED,
    TRAJ_ERROR
} traj_state_t;

typedef struct {
    float a[TRAJ_MAX_JOINTS];
    float duration;
    uint8_t action;
} traj_segment_t;

typedef struct {
    traj_state_t state;
    uint32_t frame_count;
    uint32_t total_segments;
    float total_duration;
    float elapsed_time;
    uint32_t current_segment;
    float segment_elapsed;
    traj_segment_t segments[MAX_FRAMES_PER_SEQ - 1][TRAJ_MAX_JOINTS];
    motion_frame_t frames[MAX_FRAMES_PER_SEQ];
} traj_controller_t;

bool traj_init_from_sequence(traj_controller_t *traj, const action_sequence_t *seq);
void traj_eval(traj_controller_t *traj,
               float t,
               float q_out[TRAJ_MAX_JOINTS],
               float v_out[TRAJ_MAX_JOINTS],
               bool *seg_done,
               bool *seq_done);
void traj_reset(traj_controller_t *traj);
bool traj_step(traj_controller_t *traj,
               float dt,
               float q_out[TRAJ_MAX_JOINTS],
               float v_out[TRAJ_MAX_JOINTS]);
traj_state_t traj_get_state(const traj_controller_t *traj);

#endif /* TRAJECTORY_H_ */
