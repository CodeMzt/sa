/* generated thread source file - do not edit */
#include "can_comms.h"

#if 1
static StaticTask_t can_comms_memory;
#if defined(__ARMCC_VERSION)           /* AC6 compiler */
                static uint8_t can_comms_stack[0x800] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
                #else
static uint8_t can_comms_stack[0x800] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.can_comms") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
#endif
#endif
TaskHandle_t can_comms;
void can_comms_create(void);
static void can_comms_func(void *pvParameters);
void rtos_startup_err_callback(void *p_instance, void *p_data);
void rtos_startup_common_init(void);
/* Nominal and Data bit timing configuration */

can_bit_timing_cfg_t g_canfd0_bit_timing_cfg =
{
/* Actual bitrate: 1000000 Hz. Actual sample point: 75 %. */
.baud_rate_prescaler = 1,
  .time_segment_1 = 29, .time_segment_2 = 10, .synchronization_jump_width = 4 };

#if BSP_FEATURE_CANFD_FD_SUPPORT
can_bit_timing_cfg_t g_canfd0_data_timing_cfg =
{
    /* Actual bitrate: 1000000 Hz. Actual sample point: 75 %. */
    .baud_rate_prescaler = 1,
    .time_segment_1 = 29,
    .time_segment_2 = 10,
    .synchronization_jump_width = 4
};
#endif

extern const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM];
#ifndef CANFD_PRV_GLOBAL_CFG
#define CANFD_PRV_GLOBAL_CFG

#ifdef RA_NOT_DEFINED
#undef RA_NOT_DEFINED
#endif
#define RA_NOT_DEFINED (0)

/* Buffer RAM used: RA_NOT_DEFINED bytes */
canfd_global_cfg_t g_canfd_global_cfg =
{ .global_interrupts = CANFD_CFG_GLOBAL_ERR_SOURCES, .global_config = (CANFD_CFG_TX_PRIORITY | CANFD_CFG_DLC_CHECK
        | (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC ? R_CANFD_CFDGCFG_DCS_Msk : 0U)
        | CANFD_CFG_FD_OVERFLOW | ((RA_NOT_DEFINED) << R_CANFD_CFDGCFG_ITRCP_Pos)),
  .rx_mb_config = (CANFD_CFG_RXMB_NUMBER | (CANFD_CFG_RXMB_SIZE << R_CANFD_CFDRMNB_RMPLS_Pos)), .global_err_ipl =
          CANFD_CFG_GLOBAL_ERR_IPL,
  .rx_fifo_ipl = CANFD_CFG_RX_FIFO_IPL, .rx_fifo_config =
  { ((CANFD_CFG_RXFIFO0_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO0_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO0_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO0_INT_MODE)
            | (CANFD_CFG_RXFIFO0_ENABLE)),
    ((CANFD_CFG_RXFIFO1_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO1_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO1_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO1_INT_MODE)
            | (CANFD_CFG_RXFIFO1_ENABLE)),
#if !BSP_FEATURE_CANFD_LITE
    ((CANFD_CFG_RXFIFO2_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO2_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO2_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO2_INT_MODE)
            | (CANFD_CFG_RXFIFO2_ENABLE)),
    ((CANFD_CFG_RXFIFO3_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO3_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO3_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO3_INT_MODE)
            | (CANFD_CFG_RXFIFO3_ENABLE)),
    ((CANFD_CFG_RXFIFO4_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO4_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO4_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO4_INT_MODE)
            | (CANFD_CFG_RXFIFO4_ENABLE)),
    ((CANFD_CFG_RXFIFO5_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO5_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO5_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO5_INT_MODE)
            | (CANFD_CFG_RXFIFO5_ENABLE)),
    ((CANFD_CFG_RXFIFO6_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO6_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO6_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO6_INT_MODE)
            | (CANFD_CFG_RXFIFO6_ENABLE)),
    ((CANFD_CFG_RXFIFO7_INT_THRESHOLD << R_CANFD_CFDRFCC_RFIGCV_Pos)
            | (CANFD_CFG_RXFIFO7_DEPTH << R_CANFD_CFDRFCC_RFDC_Pos)
            | (CANFD_CFG_RXFIFO7_PAYLOAD << R_CANFD_CFDRFCC_RFPLS_Pos) | (CANFD_CFG_RXFIFO7_INT_MODE)
            | (CANFD_CFG_RXFIFO7_ENABLE)),
#endif
          },
  .common_fifo_config =
  { CANFD_CFG_COMMONFIFO0,
#if !BSP_FEATURE_CANFD_LITE
    CANFD_CFG_COMMONFIFO1,
    CANFD_CFG_COMMONFIFO2, CANFD_CFG_COMMONFIFO3, CANFD_CFG_COMMONFIFO4, CANFD_CFG_COMMONFIFO5,
#endif
          } };
#undef RA_NOT_DEFINED

#endif

canfd_extended_cfg_t g_canfd0_extended_cfg =
{ .p_afl = p_canfd0_afl, .txmb_txi_enable = ((1ULL << 0) | 0ULL), .error_interrupts = (0U),
#if BSP_FEATURE_CANFD_FD_SUPPORT
    .p_data_timing      = &g_canfd0_data_timing_cfg,
#else
  .p_data_timing = NULL,
#endif
  .delay_compensation = (1),
  .p_global_cfg = &g_canfd_global_cfg, };

canfd_instance_ctrl_t g_canfd0_ctrl;
const can_cfg_t g_canfd0_cfg =
{ .channel = 0, .p_bit_timing = &g_canfd0_bit_timing_cfg, .p_callback = canfd0_callback, .p_extend =
          &g_canfd0_extended_cfg,
  .p_context = NULL, .ipl = (8),
#if defined(VECTOR_NUMBER_CAN0_COMFRX)
    .rx_irq             = VECTOR_NUMBER_CAN0_COMFRX,
#else
  .rx_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_CAN0_TX)
    .tx_irq             = VECTOR_NUMBER_CAN0_TX,
#else
  .tx_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_CAN0_CHERR)
    .error_irq             = VECTOR_NUMBER_CAN0_CHERR,
#else
  .error_irq = FSP_INVALID_VECTOR,
#endif
        };
/* Instance structure to use this module. */
const can_instance_t g_canfd0 =
{ .p_ctrl = &g_canfd0_ctrl, .p_cfg = &g_canfd0_cfg, .p_api = &g_canfd_on_canfd };
extern uint32_t g_fsp_common_thread_count;

const rm_freertos_port_parameters_t can_comms_parameters =
{ .p_context = (void*) NULL, };

void can_comms_create(void)
{
    /* Increment count so we will know the number of threads created in the RA Configuration editor. */
    g_fsp_common_thread_count++;

    /* Initialize each kernel object. */

#if 1
    can_comms = xTaskCreateStatic (
#else
                    BaseType_t can_comms_create_err = xTaskCreate(
                    #endif
                                   can_comms_func,
                                   (const char*) "can_comms", 0x800 / 4, // In words, not bytes
                                   (void*) &can_comms_parameters, //pvParameters
                                   3,
#if 1
                                   (StackType_t*) &can_comms_stack,
                                   (StaticTask_t*) &can_comms_memory
#else
                        & can_comms
                        #endif
                                   );

#if 1
    if (NULL == can_comms)
    {
        rtos_startup_err_callback (can_comms, 0);
    }
#else
                    if (pdPASS != can_comms_create_err)
                    {
                        rtos_startup_err_callback(can_comms, 0);
                    }
                    #endif
}
static void can_comms_func(void *pvParameters)
{
    /* Initialize common components */
    rtos_startup_common_init ();

    /* Initialize each module instance. */

#if (1 == BSP_TZ_NONSECURE_BUILD) && (1 == 1)
                    /* When FreeRTOS is used in a non-secure TrustZone application, portALLOCATE_SECURE_CONTEXT must be called prior
                     * to calling any non-secure callable function in a thread. The parameter is unused in the FSP implementation.
                     * If no slots are available then configASSERT() will be called from vPortSVCHandler_C(). If this occurs, the
                     * application will need to either increase the value of the "Process Stack Slots" Property in the rm_tz_context
                     * module in the secure project or decrease the number of threads in the non-secure project that are allocating
                     * a secure context. Users can control which threads allocate a secure context via the Properties tab when
                     * selecting each thread. Note that the idle thread in FreeRTOS requires a secure context so the application
                     * will need at least 1 secure context even if no user threads make secure calls. */
                     portALLOCATE_SECURE_CONTEXT(0);
                    #endif

    /* Enter user code for this thread. Pass task handle. */
    can_comms_entry (pvParameters);
}
