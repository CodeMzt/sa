/* generated thread header file - do not edit */
#ifndef VOICE_COMMAND_H_
#define VOICE_COMMAND_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void voice_command_entry(void * pvParameters);
                #else
extern void voice_command_entry(void *pvParameters);
#endif
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_i2s_api.h"
#include "r_ssi.h"
FSP_HEADER
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer_audio_clk;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer_audio_clk_ctrl;
extern const timer_cfg_t g_timer_audio_clk_cfg;

#ifndef NULL
void NULL(timer_callback_args_t *p_args);
#endif
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer6;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer6_ctrl;
extern const transfer_cfg_t g_transfer6_cfg;
/** SSI Instance. */
extern const i2s_instance_t g_i2s0;

/** Access the SSI instance using these structures when calling API functions directly (::p_api is not used). */
extern ssi_instance_ctrl_t g_i2s0_ctrl;
extern const i2s_cfg_t g_i2s0_cfg;

#ifndef i2s0_callback
void i2s0_callback(i2s_callback_args_t *p_args);
#endif
FSP_FOOTER
#endif /* VOICE_COMMAND_H_ */
