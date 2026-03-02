/**
 * @file    ui_app.c
 * @brief   UI 核心实现
 */

#include "ui_app.h"
#include "sys_log.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include "voice_command.h"
#include "wifi_debug.h"
#include "drv_wifi.h"
#include "nvm_types.h"
#include "nvm_manager.h"

/* -------------------------------------------------------------------------- */
/* Private Variables                                                          */
/* -------------------------------------------------------------------------- */
static system_status_t last_sys_status = {0};

/* 示教模式统一上下文结构体 */
typedef struct {
    uint8_t group_idx;
    uint8_t frame_idx;
    lv_obj_t * btn_ptr;
    uint16_t * p_duration;
} teach_frame_ctx_t;

typedef struct {
    uint8_t group_idx;
    uint8_t frame_idx;
    uint16_t duration;
} action_select_ctx_t;

/* 页面状态枚举 */
typedef enum {
    PAGE_HOME = 0,
    PAGE_TOUCH,
    PAGE_VOICE,
    PAGE_DEBUG,
    PAGE_TEACH_MAIN,
    PAGE_TEACH_MONITOR,
    PAGE_TEACH_RECORD,
    PAGE_TEACH_FRAMES,
    PAGE_TEACH_ZERO
} ui_page_t;

static ui_page_t g_current_page = PAGE_HOME;

/* 全局定时器（用于示教模式，需要在页面切换时清理） */
static lv_timer_t * g_teach_monitor_timer = NULL;
static lv_timer_t * g_teach_record_angle_timer = NULL;

/* UI 对象句柄 */
static lv_obj_t * label_status_eth;
static lv_obj_t * label_status_can;
static lv_obj_t * label_status_mic;
static lv_obj_t * label_status_wifi;
static lv_obj_t * label_queue_info;
static lv_obj_t * btn_start_stop;
static lv_obj_t * label_btn_start;
static lv_obj_t * label_voice_status;
static lv_obj_t * win_estop = NULL; // 急停窗口句柄

/* 样式对象 */
static lv_style_t style_scr_main;  // 全局背景
static lv_style_t style_btn_std;   // 标准按钮
static lv_style_t style_btn_red;   // 红色按钮 (急停)
static lv_style_t style_btn_green; // 绿色按钮 (启动)
static lv_style_t style_container; // 浅色容器


/* -------------------------------------------------------------------------- */
/* Private Defines                                                            */
/* -------------------------------------------------------------------------- */

/* 颜色定义 (Light Theme) */
#define COL_BG          lv_color_hex(0xFFFFFF)
#define COL_TEXT_MAIN   lv_color_hex(0x222222)
#define COL_TEXT_LIGHT  lv_color_hex(0xFFFFFF)
#define COL_BORDER      lv_color_hex(0xDDDDDD)

#define COL_BLUE        lv_color_hex(0x007AFF)
#define COL_RED         lv_color_hex(0xFF3B30)
#define COL_GREEN       lv_color_hex(0x34C759)
#define COL_GRAY_DARK   lv_color_hex(0x8E8E93)
#define COL_GRAY_LIGHT  lv_color_hex(0xF2F2F7)

/* 统一反馈恢复时长（毫秒） */
#define UI_FEEDBACK_RESTORE_MS   (1200U)

/* -------------------------------------------------------------------------- */
/* Private function prototypes                                                */
/* -------------------------------------------------------------------------- */

static void init_styles(void);
static void cleanup_ui_pointers(void);
static void create_global_layout(void);
static void create_home_page(void);
static void create_touch_page(void);
static void create_voice_page(void);
static void create_debug_page(void);

static void event_nav_cb(lv_event_t * e);
static void event_back_confirm_req_cb(lv_event_t * e);
static void event_mbox_cb(lv_event_t * e);
static void event_start_cb(lv_event_t * e);
static void event_voice_start_cb(lv_event_t * e);
static void event_estop_trigger_cb(lv_event_t * e);
static void event_estop_reset_cb(lv_event_t * e);
static void event_instrument_cb(lv_event_t * e);
static void event_queue_back_cb(lv_event_t * e);
static void event_voice_queue_back_cb(lv_event_t * e);
static lv_obj_t* create_status_label(lv_obj_t * parent, const char* text);

static void create_teach_page(void);
static void event_teach_submode_cb(lv_event_t * e);
static void create_teach_monitor_page(void);
static void teach_monitor_update_cb(lv_timer_t * timer);
static void event_teach_monitor_back_cb(lv_event_t * e);
static void create_teach_record_page(void);
static void create_teach_zero_page(void);
static void event_teach_group_selected_cb(lv_event_t * e);
static void create_teach_record_frames_page(uint8_t group_idx);
static void teach_record_angle_update_cb(lv_timer_t * timer);
static void event_teach_record_back_cb(lv_event_t * e);
static void event_teach_frame_record_cb(lv_event_t * e);
static void event_teach_record_main_back_cb(lv_event_t * e);
static void event_teach_zero_back_cb(lv_event_t * e);
static void event_teach_zero_set_cb(lv_event_t * e);
static void event_teach_frame_duration_minus_cb(lv_event_t * e);
static void event_teach_frame_duration_plus_cb(lv_event_t * e);
static void event_teach_frame_last_action_select_cb(lv_event_t * e);
static void frame_feedback_timer_cb(lv_timer_t * timer);
static void event_teach_save_config_cb(lv_event_t * e);
static void zero_feedback_timer_cb(lv_timer_t * timer);

/* -------------------------------------------------------------------------- */
/* Core Interface Functions                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief UI 初始化
 */
void ui_app_init(void) {
    init_styles();
    lv_obj_add_style(lv_scr_act(), &style_scr_main, 0);
    create_global_layout();
    create_home_page();
    memset((void*)&last_sys_status, 0xFF, sizeof(system_status_t));
}

/**
 * @brief 更新 UI 状态
 */
void ui_update_status(void) {
    if (g_sys_status.is_eth_connected != last_sys_status.is_eth_connected) {
        lv_obj_set_style_bg_color(label_status_eth,
            g_sys_status.is_eth_connected ? COL_GREEN : COL_GRAY_DARK, 0);
        last_sys_status.is_eth_connected = g_sys_status.is_eth_connected;
    }

    if (g_sys_status.is_can_connected != last_sys_status.is_can_connected) {
        lv_obj_set_style_bg_color(label_status_can,
            g_sys_status.is_can_connected ? COL_GREEN : COL_GRAY_DARK, 0);
        last_sys_status.is_can_connected = g_sys_status.is_can_connected;
    }

    if (g_sys_status.is_mic_connected != last_sys_status.is_mic_connected) {
        lv_obj_set_style_bg_color(label_status_mic,
            g_sys_status.is_mic_connected ? COL_GREEN : COL_GRAY_DARK, 0);
        last_sys_status.is_mic_connected = g_sys_status.is_mic_connected;
    }

    if (g_sys_status.is_wifi_connected != last_sys_status.is_wifi_connected) {
        lv_obj_set_style_bg_color(label_status_wifi,
            g_sys_status.is_wifi_connected ? COL_GREEN : COL_GRAY_DARK, 0);
        last_sys_status.is_wifi_connected = g_sys_status.is_wifi_connected;
    }

    if (label_queue_info && lv_obj_is_valid(label_queue_info)) {
        if (strcmp((char*)g_sys_status.queue_list, (char*)last_sys_status.queue_list) != 0) {
            lv_label_set_text(label_queue_info, g_sys_status.queue_list);
            strncpy((char*)last_sys_status.queue_list, (char*)g_sys_status.queue_list, sizeof(last_sys_status.queue_list));
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 样式定义                                                                   */
/* -------------------------------------------------------------------------- */

#define TEACH_ZERO_BUTTON_NUM 5

typedef struct {
    uint8_t idx;
    uint8_t motor_id;
    lv_obj_t * btn;
    const char * default_text;
} teach_zero_ctx_t;

static lv_timer_t * g_frame_feedback_timers[TEACH_FRAMES_PER_GROUP] = {NULL};
static teach_zero_ctx_t g_teach_zero_ctx[TEACH_ZERO_BUTTON_NUM];
static lv_timer_t * g_zero_feedback_timers[TEACH_ZERO_BUTTON_NUM] = {NULL};

/**
 * @brief 清理 UI 全局指针（切页时调用，防止悬空引用和定时器泄漏）
 */
static void cleanup_ui_pointers(void) {
    /* 清理定时器（防止卡死） */
    if (g_teach_monitor_timer) {
        lv_timer_del(g_teach_monitor_timer);
        g_teach_monitor_timer = NULL;
    }
    if (g_teach_record_angle_timer) {
        lv_timer_del(g_teach_record_angle_timer);
        g_teach_record_angle_timer = NULL;
    }
    for (int i = 0; i < TEACH_FRAMES_PER_GROUP; i++) {
        if (g_frame_feedback_timers[i]) {
            lv_timer_del(g_frame_feedback_timers[i]);
            g_frame_feedback_timers[i] = NULL;
        }
    }
    for (int i = 0; i < TEACH_ZERO_BUTTON_NUM; i++) {
        if (g_zero_feedback_timers[i]) {
            lv_timer_del(g_zero_feedback_timers[i]);
            g_zero_feedback_timers[i] = NULL;
        }
        g_teach_zero_ctx[i].btn = NULL;
    }
    
    /* 清理 UI 指针 */
    label_queue_info = NULL;
    label_voice_status = NULL;
    label_btn_start = NULL;
    btn_start_stop = NULL;
}

/**
 * @brief 保存指针用于帧页面内的按钮反馈
 * 每个帧页面保存其对应的3个按钮指针
 */
static lv_obj_t * g_frame_btns[3] = {NULL, NULL, NULL};

/**
 * @brief Frame 反馈上下文结构体（用于2秒自动恢复定时器）
 */
typedef struct {
    uint8_t frame_idx;
    lv_obj_t * btn;
} frame_feedback_ctx_t;

static frame_feedback_ctx_t g_frame_feedback_ctx[TEACH_FRAMES_PER_GROUP];

/**
 * @brief 初始化 UI 样式
 */
static void init_styles(void) { 
    lv_style_init(&style_scr_main);
    lv_style_set_bg_color(&style_scr_main, COL_BG);
    lv_style_set_bg_opa(&style_scr_main, LV_OPA_COVER);
    lv_style_set_text_color(&style_scr_main, COL_TEXT_MAIN);

    lv_style_init(&style_container);
    lv_style_set_bg_color(&style_container, COL_GRAY_LIGHT);
    lv_style_set_bg_opa(&style_container, LV_OPA_COVER);
    lv_style_set_border_width(&style_container, 0);
    lv_style_set_pad_all(&style_container, 0);

    lv_style_init(&style_btn_std);
    lv_style_set_bg_color(&style_btn_std, COL_BLUE);
    lv_style_set_bg_opa(&style_btn_std, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_std, 0);
    lv_style_set_radius(&style_btn_std, 4);
    lv_style_set_shadow_width(&style_btn_std, 0);
    lv_style_set_text_color(&style_btn_std, COL_TEXT_LIGHT);

    lv_style_init(&style_btn_red);
    lv_style_set_bg_color(&style_btn_red, COL_RED);
    lv_style_set_bg_opa(&style_btn_red, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_red, 0);
    lv_style_set_radius(&style_btn_red, 4);
    lv_style_set_shadow_width(&style_btn_red, 0);
    lv_style_set_text_color(&style_btn_red, COL_TEXT_LIGHT);

    lv_style_init(&style_btn_green);
    lv_style_set_bg_color(&style_btn_green, COL_GREEN);
    lv_style_set_bg_opa(&style_btn_green, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_green, 0);
    lv_style_set_radius(&style_btn_green, 4);
    lv_style_set_shadow_width(&style_btn_green, 0);
    lv_style_set_text_color(&style_btn_green, COL_TEXT_LIGHT);
}

/* -------------------------------------------------------------------------- */
/* 页面创建                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 创建全局布局
 */
static void create_global_layout(void) {
    lv_obj_t * top = lv_layer_top();

    /* === 急停按钮 (右下角, 宽90, 高60) === */
    lv_obj_t * btn_estop = lv_btn_create(top);
    lv_obj_add_style(btn_estop, &style_btn_red, 0);
    lv_obj_set_size(btn_estop, 80, 60);
    lv_obj_align(btn_estop, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_clear_flag(btn_estop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_estop, event_estop_trigger_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl = lv_label_create(btn_estop);
    lv_label_set_text(lbl, "E-STOP");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    static lv_coord_t col_dsc[] = {75, 75, 75, 75, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {30, LV_GRID_TEMPLATE_LAST};

    lv_obj_t * bar = lv_obj_create(top);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, COL_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 2, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(bar, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(bar, row_dsc, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    label_status_eth = create_status_label(bar, "ETH");
    lv_obj_set_grid_cell(label_status_eth, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    label_status_can = create_status_label(bar, "CAN");
    lv_obj_set_grid_cell(label_status_can, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    label_status_mic = create_status_label(bar, "MIC");
    lv_obj_set_grid_cell(label_status_mic, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    label_status_wifi = create_status_label(bar, "WiFi");
    lv_obj_set_grid_cell(label_status_wifi, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
}

static lv_obj_t* create_status_label(lv_obj_t * parent, const char* text) {
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, COL_GRAY_DARK, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, COL_BG, 0);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_TEXT_LIGHT, 0);
    lv_obj_center(lbl);
    return obj;
}

/**
 * @brief 创建主页
 */
static void create_home_page(void) {
    lv_obj_clean(lv_scr_act());
    cleanup_ui_pointers();

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Surgideliver Arm");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 260, 240);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    #define ADD_BIG_BTN(txt, id) \
        do { \
            lv_obj_t * btn = lv_btn_create(cont); \
            lv_obj_set_width(btn, lv_pct(100)); \
            lv_obj_set_height(btn, 55); \
            lv_obj_add_style(btn, &style_btn_std, 0); \
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE); \
            lv_obj_add_event_cb(btn, event_nav_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)id); \
            lv_obj_t * l = lv_label_create(btn); \
            lv_label_set_text(l, txt); \
            lv_obj_center(l); \
        } while(0)

    ADD_BIG_BTN("TOUCH MODE", 1);
    ADD_BIG_BTN("VOICE MODE", 2);
    ADD_BIG_BTN("TEACH MODE", 4);
    ADD_BIG_BTN("DEBUG MENU", 3);
}

/**
 * @brief 创建触摸模式页面
 */
static void create_touch_page(void) {
    lv_obj_clean(lv_scr_act());
    cleanup_ui_pointers();

    /* === 队列显示 (左侧, 宽度220, 高度2行约40) === */
    label_queue_info = lv_label_create(lv_scr_act());
    update_queue_display_string();
    lv_label_set_text(label_queue_info, g_sys_status.queue_list);
    lv_obj_set_width(label_queue_info, 220);
    lv_obj_set_height(label_queue_info, 40);
    lv_obj_set_style_text_color(label_queue_info, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_align(label_queue_info, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(label_queue_info, LV_ALIGN_TOP_LEFT, 10, 45);

    /* === UNDO 按钮 (队列右侧, 宽70, 高40) === */
    lv_obj_t * btn_undo = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_undo, 70, 40);
    lv_obj_align(btn_undo, LV_ALIGN_TOP_RIGHT, -10, 45);
    lv_obj_add_style(btn_undo, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_undo, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_undo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_undo, event_queue_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_undo = lv_label_create(btn_undo);
    lv_label_set_text(lbl_undo, "UNDO");
    lv_obj_center(lbl_undo);

    /* === 分隔线 === */
    lv_obj_t * line = lv_obj_create(lv_scr_act());
    lv_obj_set_size(line, 300, 2);
    lv_obj_set_style_bg_color(line, COL_BORDER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 95);

    /* === 器械按钮网格 === */
    lv_obj_t * grid = lv_obj_create(lv_scr_act());
    lv_obj_set_size(grid, 300, 220);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {140, 140, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {95, 95, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(grid, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, 0);

    const char* items[] = {"SCALPEL", "HEMOSTAT", "FORCEPS"};
    for(int i=0; i<3; i++) {
        lv_obj_t * btn = lv_btn_create(grid);
        lv_obj_add_style(btn, &style_btn_std, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i%2, 1, LV_GRID_ALIGN_STRETCH, i/2, 1);
        lv_obj_add_event_cb(btn, event_instrument_cb, LV_EVENT_CLICKED, (void*)items[i]);
        lv_label_set_text(lv_label_create(btn), items[i]);
    }

    /* === BACK 按钮 (左下角, 宽80, 高60) === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_back_confirm_req_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);

    /* === START 按钮 (正中间, 宽110, 高60) === */
    btn_start_stop = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_start_stop, 110, 60);
    lv_obj_align(btn_start_stop, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(btn_start_stop, &style_btn_green, 0);
    lv_obj_clear_flag(btn_start_stop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_start_stop, event_start_cb, LV_EVENT_CLICKED, NULL);

    label_btn_start = lv_label_create(btn_start_stop);
    lv_label_set_text(label_btn_start, "START");
    lv_obj_center(label_btn_start);
}

/**
 * @brief 创建语音模式页面
 */
static void create_voice_page(void) {
    lv_obj_clean(lv_scr_act());
    cleanup_ui_pointers();

    /* === 队列显示 (左侧, 宽度220, 高度2行约40) === */
    label_queue_info = lv_label_create(lv_scr_act());
    update_queue_display_string();
    lv_label_set_text(label_queue_info, g_sys_status.queue_list);
    lv_obj_set_width(label_queue_info, 220);
    lv_obj_set_height(label_queue_info, 40);
    lv_obj_set_style_text_color(label_queue_info, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_align(label_queue_info, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(label_queue_info, LV_ALIGN_TOP_LEFT, 10, 45);

    /* === UNDO 按钮 (队列右侧, 宽70, 高40) === */
    lv_obj_t * btn_undo = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_undo, 70, 40);
    lv_obj_align(btn_undo, LV_ALIGN_TOP_RIGHT, -10, 45);
    lv_obj_add_style(btn_undo, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_undo, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_undo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_undo, event_voice_queue_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_undo = lv_label_create(btn_undo);
    lv_label_set_text(lbl_undo, "UNDO");
    lv_obj_center(lbl_undo);

    /* === 分隔线 === */
    lv_obj_t * line = lv_obj_create(lv_scr_act());
    lv_obj_set_size(line, 300, 2);
    lv_obj_set_style_bg_color(line, COL_BORDER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 95);

    /* === 状态显示框 === */
    lv_obj_t * box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box, 280, 120);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_color(box, COL_GRAY_LIGHT, 0);
    lv_obj_set_style_border_color(box, COL_BORDER, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    label_voice_status = lv_label_create(box);
    lv_label_set_text(label_voice_status, "Click bottom to start");
    lv_obj_set_style_text_align(label_voice_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_voice_status, COL_TEXT_MAIN, 0);
    lv_obj_center(label_voice_status);

    /* === BACK 按钮 === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_back_confirm_req_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);

    /* === START 按钮 === */
    btn_start_stop = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_start_stop, 110, 60);
    lv_obj_align(btn_start_stop, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(btn_start_stop, &style_btn_green, 0);
    lv_obj_clear_flag(btn_start_stop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_start_stop, event_voice_start_cb, LV_EVENT_CLICKED, NULL);

    label_btn_start = lv_label_create(btn_start_stop);
    lv_label_set_text(label_btn_start, "START");
    lv_obj_center(label_btn_start);
}

/**
 * @brief 创建调试页面
 */
static void create_debug_page(void) {
    lv_obj_clean(lv_scr_act());

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "WIFI DEBUG MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    /* === 状态显示框（自适应，避免与底部按钮重叠）=== */
    lv_coord_t box_w = (LV_HOR_RES > 300) ? 280 : (LV_HOR_RES - 20);
    lv_coord_t box_h = (LV_VER_RES >= 420) ? 130 : 110;
    lv_coord_t box_y = 80;
    lv_coord_t bottom_safe_top = LV_VER_RES - 85; /* 预留底部按钮区域 */
    if (box_y + box_h > bottom_safe_top) {
        box_y = bottom_safe_top - box_h;
        if (box_y < 70) {
            box_y = 70;
        }
    }

    lv_obj_t * box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box, box_w, box_h);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, box_y);
    lv_obj_set_style_bg_color(box, COL_GRAY_LIGHT, 0);
    lv_obj_set_style_border_color(box, COL_BORDER, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_pad_left(box, 8, 0);
    lv_obj_set_style_pad_right(box, 8, 0);
    lv_obj_set_style_pad_top(box, 8, 0);
    lv_obj_set_style_pad_bottom(box, 8, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* WIFI 图标 */
    lv_obj_t * icon = lv_label_create(box);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, COL_BLUE, 0);

    /* 主提示 */
    lv_obj_t * lbl_main = lv_label_create(box);
    lv_label_set_text(lbl_main, "Link Active");
    lv_obj_set_style_text_font(lbl_main, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_main, COL_TEXT_MAIN, 0);

    /* 副提示 */
    lv_obj_t * lbl_sub = lv_label_create(box);
    lv_label_set_text(lbl_sub, "Please operate via mobile APP.\nNo local inputs required.");
    lv_obj_set_width(lbl_sub, lv_pct(100));
    lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_sub, COL_GRAY_DARK, 0);

    /* === BACK 按钮 === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_back_confirm_req_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

/* -------------------------------------------------------------------------- */
/* 事件回调                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 导航事件回调
 * @param e LVGL 事件对象
 */
static void event_nav_cb(lv_event_t * e) {
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    label_queue_info = NULL;
    g_sys_status.is_debug_mode_active = false;

    switch(id) {
        case 0: 
            g_current_page = PAGE_HOME;
            create_home_page(); 
            break;
        case 1: 
            g_current_page = PAGE_TOUCH;
            create_touch_page(); 
            break;
        case 2: 
            g_current_page = PAGE_VOICE;
            label_voice_status = NULL;
            create_voice_page(); 
            break;
        case 3: 
            g_current_page = PAGE_DEBUG;
            g_sys_status.is_debug_mode_active = true;
            create_debug_page(); 
            break;
        case 4: 
            g_current_page = PAGE_TEACH_MAIN;
            user_on_teach_enter();
            create_teach_page(); 
            break;
    }
}

/**
 * @brief BACK 按钮事件回调
 * 根据当前页面状态决定返回逻辑
 */
static void event_back_confirm_req_cb(lv_event_t * e) {
    switch(g_current_page) {
        case PAGE_TOUCH:
        case PAGE_VOICE:
        case PAGE_DEBUG:
            /* 从一级页面返回HOME时弹确认框 */
            {
                static const char * btns[] = {"YES", "NO", ""};
                lv_obj_t * mbox = lv_msgbox_create(NULL, "Confirm", "Return to Home?", btns, false);
                lv_obj_center(mbox);
                lv_obj_add_event_cb(mbox, event_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            }
            break;
        
        case PAGE_TEACH_MAIN:
            /* TEACH 主页返回HOME也弹确认框 */
            {
                static const char * btns[] = {"YES", "NO", ""};
                lv_obj_t * mbox = lv_msgbox_create(NULL, "Confirm", "Return to Home?", btns, false);
                lv_obj_center(mbox);
                lv_obj_add_event_cb(mbox, event_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            }
            break;
        
        default:
            break;
    }
}

/**
 * @brief 消息框事件回调
 * @param e LVGL 事件对象
 */
static void event_mbox_cb(lv_event_t * e) {
    lv_obj_t * mbox = lv_event_get_current_target(e);
    const char * btn_txt = lv_msgbox_get_active_btn_text(mbox);

    if(btn_txt && strcmp(btn_txt, "YES") == 0) {
        if (g_current_page == PAGE_TEACH_MAIN) {
            user_on_teach_exit();
        }

        if (g_sys_status.is_running) {
            g_sys_status.is_running = false;
            user_on_stop();
        }
        if (g_sys_status.is_voice_command_running) {
            g_sys_status.is_voice_command_running = false;
            R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
            user_on_voice_stop();
        }

        g_sys_status.is_debug_mode_active = false;

        clear_act_queue();
        update_queue_display_string();

        if (label_voice_status != NULL && lv_obj_is_valid(label_voice_status)) {
            lv_label_set_text(label_voice_status, "Click bottom to start");
        }

        lv_msgbox_close(mbox);

        create_home_page();
    } else {
        lv_msgbox_close(mbox);
    }
}

/**
 * @brief 急停触发事件回调
 * @param e LVGL 事件对象
 */
static void event_estop_trigger_cb(lv_event_t * e) {    
    if (win_estop == NULL) {
        /* 创建全屏急停界面 */
        win_estop = lv_obj_create(lv_layer_top());
        lv_obj_set_size(win_estop, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(win_estop, 0, 0);
        lv_obj_set_style_bg_color(win_estop, COL_RED, 0); /* 修复：补齐参数 */
        lv_obj_set_style_bg_opa(win_estop, LV_OPA_COVER, 0);
        lv_obj_clear_flag(win_estop, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * l = lv_label_create(win_estop);
        lv_label_set_text(l, "EMERGENCY\nSTOP TRIGGERED!");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, COL_TEXT_LIGHT, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -40);

        lv_obj_t * btn_reset = lv_btn_create(win_estop);
        lv_obj_set_size(btn_reset, 160, 60);
        lv_obj_align(btn_reset, LV_ALIGN_CENTER, 0, 60);
        lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(btn_reset, COL_RED, 0);
        lv_obj_add_event_cb(btn_reset, event_estop_reset_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t * l_rst = lv_label_create(btn_reset);
        lv_label_set_text(l_rst, "UNLOCK / RESET");
        lv_obj_center(l_rst);
    }

    if (g_sys_status.is_running) {
        g_sys_status.is_running = false;
        user_on_stop();
    }
    if (g_sys_status.is_voice_command_running) {
        g_sys_status.is_voice_command_running = false;
        user_on_voice_stop();
    }
    user_on_emergency_stop();
}

/**
 * @brief 急停复位事件回调
 * @param e LVGL 事件对象
 */
static void event_estop_reset_cb(lv_event_t * e) {
    if (win_estop != NULL) {
        lv_obj_del(win_estop);
        win_estop = NULL;
    }
    user_on_estop_reset();
}

/**
 * @brief 语音启动事件回调
 * @param e LVGL 事件对象
 */
static void event_voice_start_cb(lv_event_t * e) {
    g_sys_status.is_voice_command_running = !g_sys_status.is_voice_command_running;

    if (g_sys_status.is_voice_command_running) {
        lv_obj_remove_style(btn_start_stop, &style_btn_green, 0);
        lv_obj_add_style(btn_start_stop, &style_btn_red, 0);
        lv_label_set_text(label_btn_start, "STOP");

        if (label_voice_status != NULL && lv_obj_is_valid(label_voice_status)) {
            lv_label_set_text(label_voice_status, "Listening...");
        }

        R_BSP_IrqEnable(g_i2s0_cfg.rxi_irq);
        user_on_voice_start();
    } else {
        R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
        lv_obj_remove_style(btn_start_stop, &style_btn_red, 0);
        lv_obj_add_style(btn_start_stop, &style_btn_green, 0);
        lv_label_set_text(label_btn_start, "START");

        if (label_voice_status != NULL && lv_obj_is_valid(label_voice_status)) {
            lv_label_set_text(label_voice_status, "Click bottom to start");
        }
        user_on_voice_stop();
    }
}

/**
 * @brief 启动按钮事件回调
 * @param e LVGL 事件对象
 */
static void event_start_cb(lv_event_t * e) {
    g_sys_status.is_running = !g_sys_status.is_running;

    if (g_sys_status.is_running) {
        lv_obj_remove_style(btn_start_stop, &style_btn_green, 0);
        lv_obj_add_style(btn_start_stop, &style_btn_red, 0);
        lv_label_set_text(label_btn_start, "STOP");

        user_on_start();
    } else {
        lv_obj_remove_style(btn_start_stop, &style_btn_red, 0);
        lv_obj_add_style(btn_start_stop, &style_btn_green, 0);
        lv_label_set_text(label_btn_start, "START");

        user_on_stop();
    }
}

/**
 * @brief 器械选择事件回调
 * @param e LVGL 事件对象
 */
static void event_instrument_cb(lv_event_t * e) {
    if (g_sys_status.is_running) {
        return;
    }

    const char* name = (const char*)lv_event_get_user_data(e);
    instrument_t inst = INSTRUMENT_NONE;

    if (strcmp(name, "SCALPEL") == 0) {
        inst = INSTRUMENT_SCALPEL;
    } else if (strcmp(name, "HEMOSTAT") == 0) {
        inst = INSTRUMENT_HEMOSTAT;
    } else if (strcmp(name, "FORCEPS") == 0) {
        inst = INSTRUMENT_FORCEPS;
    }

    if (add_instrument_to_queue(inst)) {
        update_queue_display_string();
        ui_update_status();
    }
}

/**
 * @brief 队列返回事件回调
 * @param e LVGL 事件对象
 */
static void event_queue_back_cb(lv_event_t * e) {
    if (g_sys_status.is_running) {
        return;
    }

    if (g_sys_status.act_queue_count > 0) {
        remove_instrument_from_queue(g_sys_status.act_queue_count - 1);
        update_queue_display_string();
        ui_update_status();
    }
}

/**
 * @brief 语音队列返回事件回调
 * @param e LVGL 事件对象
 */
static void event_voice_queue_back_cb(lv_event_t * e) {
    if (g_sys_status.act_queue_count > 0) {
        remove_instrument_from_queue(g_sys_status.act_queue_count - 1);
        update_queue_display_string();
        ui_update_status();
    }
}

/* -------------------------------------------------------------------------- */
/* Public API Functions                                                       */
/* -------------------------------------------------------------------------- */

bool ui_add_instrument_to_queue(instrument_t inst) {
    if (add_instrument_to_queue(inst)) {
        update_queue_display_string();
        ui_update_status();
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* 示教模式页面                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 创建示教模式页面（子模式选择）
 */
static void create_teach_page(void) {
    lv_obj_clean(lv_scr_act());

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "TEACH MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 260, 210);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    #define ADD_TEACH_BTN(txt, id) \
        do { \
            lv_obj_t * btn = lv_btn_create(cont); \
            lv_obj_set_width(btn, lv_pct(100)); \
            lv_obj_set_height(btn, 55); \
            lv_obj_add_style(btn, &style_btn_std, 0); \
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE); \
            lv_obj_add_event_cb(btn, event_teach_submode_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)id); \
            lv_obj_t * l = lv_label_create(btn); \
            lv_label_set_text(l, txt); \
            lv_obj_center(l); \
        } while(0)

    ADD_TEACH_BTN("MONITOR", 1);
    ADD_TEACH_BTN("RECORD", 2);
    ADD_TEACH_BTN("ZERO", 3);

    /* === BACK 按钮 === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_back_confirm_req_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

/**
 * @brief 示教模式子模式选择回调
 */
static void event_teach_submode_cb(lv_event_t * e) {
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    switch(id) {
        case 1: 
            g_current_page = PAGE_TEACH_MONITOR;
            create_teach_monitor_page(); 
            break;
        case 2: 
            g_current_page = PAGE_TEACH_RECORD;
            create_teach_record_page(); 
            break;
        case 3:
            g_current_page = PAGE_TEACH_ZERO;
            create_teach_zero_page();
            break;
    }
}

/**
 * @brief 创建示教零点标定页面
 */
static void create_teach_zero_page(void) {
    lv_obj_clean(lv_scr_act());

    for (int i = 0; i < TEACH_ZERO_BUTTON_NUM; i++) {
        if (g_zero_feedback_timers[i]) {
            lv_timer_del(g_zero_feedback_timers[i]);
            g_zero_feedback_timers[i] = NULL;
        }
    }

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "ZERO CALIB MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 310, 225);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {150, 150, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {66, 66, 66, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);

    static const uint8_t motor_ids[TEACH_ZERO_BUTTON_NUM] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4,
        ROBSTRIDE_MOTOR_ID_GRIPPER,
    };

    static const char * btn_texts[TEACH_ZERO_BUTTON_NUM] = {
        "J1 ZERO",
        "J2 ZERO",
        "J3 ZERO",
        "J4 ZERO",
        "GRIPPER ZERO",
    };

    for (uint8_t i = 0; i < TEACH_ZERO_BUTTON_NUM; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        lv_obj_add_style(btn, &style_btn_std, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        int row = i / 2;
        int col = i % 2;
        if (i == 4) {
            lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);
            lv_obj_set_width(btn, 170);
        } else {
            lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        }

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_texts[i]);
        lv_obj_center(lbl);

        g_teach_zero_ctx[i].idx = i;
        g_teach_zero_ctx[i].motor_id = motor_ids[i];
        g_teach_zero_ctx[i].btn = btn;
        g_teach_zero_ctx[i].default_text = btn_texts[i];

        lv_obj_add_event_cb(btn, event_teach_zero_set_cb, LV_EVENT_CLICKED, (void*)&g_teach_zero_ctx[i]);
    }

    /* === BACK 按钮（直接返回TEACH主页，不弹确认框） === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_teach_zero_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

/**
 * @brief 零点标定页面返回回调
 */
static void event_teach_zero_back_cb(lv_event_t * e) {
    FSP_PARAMETER_NOT_USED(e);
    for (int i = 0; i < TEACH_ZERO_BUTTON_NUM; i++) {
        if (g_zero_feedback_timers[i]) {
            lv_timer_del(g_zero_feedback_timers[i]);
            g_zero_feedback_timers[i] = NULL;
        }
    }
    g_current_page = PAGE_TEACH_MAIN;
    create_teach_page();
}

/**
 * @brief 零点标定按钮点击回调
 */
static void event_teach_zero_set_cb(lv_event_t * e) {
    teach_zero_ctx_t * ctx = (teach_zero_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->btn || !lv_obj_is_valid(ctx->btn)) {
        return;
    }

    fsp_err_t err = robstride_set_zero(ctx->motor_id);

    lv_obj_remove_style(ctx->btn, &style_btn_std, 0);
    lv_obj_remove_style(ctx->btn, &style_btn_green, 0);
    lv_obj_remove_style(ctx->btn, &style_btn_red, 0);

    lv_obj_t * lbl = lv_obj_get_child(ctx->btn, 0);
    if (err == FSP_SUCCESS) {
        lv_obj_add_style(ctx->btn, &style_btn_green, 0);
        if (lbl) {
            lv_label_set_text(lbl, "Zeroed!");
        }
    } else {
        lv_obj_add_style(ctx->btn, &style_btn_red, 0);
        if (lbl) {
            lv_label_set_text(lbl, "Failed");
        }
    }

    if (ctx->idx < TEACH_ZERO_BUTTON_NUM) {
        if (g_zero_feedback_timers[ctx->idx]) {
            lv_timer_del(g_zero_feedback_timers[ctx->idx]);
            g_zero_feedback_timers[ctx->idx] = NULL;
        }
        g_zero_feedback_timers[ctx->idx] = lv_timer_create(zero_feedback_timer_cb, UI_FEEDBACK_RESTORE_MS, (void*)ctx);
    }
}

/**
 * @brief 零点标定反馈恢复定时器回调
 */
static void zero_feedback_timer_cb(lv_timer_t * timer) {
    teach_zero_ctx_t * ctx = (teach_zero_ctx_t *)timer->user_data;
    if (ctx && ctx->btn && lv_obj_is_valid(ctx->btn)) {
        lv_obj_remove_style(ctx->btn, &style_btn_green, 0);
        lv_obj_remove_style(ctx->btn, &style_btn_red, 0);
        lv_obj_add_style(ctx->btn, &style_btn_std, 0);
        lv_obj_t * lbl = lv_obj_get_child(ctx->btn, 0);
        if (lbl && ctx->default_text) {
            lv_label_set_text(lbl, ctx->default_text);
        }
    }

    if (ctx && ctx->idx < TEACH_ZERO_BUTTON_NUM) {
        g_zero_feedback_timers[ctx->idx] = NULL;
    }

    lv_timer_del(timer);
}

/**
 * @brief 创建示教监测页面（数据监测，200ms 刷新）
 */
static void create_teach_monitor_page(void) {
    lv_obj_clean(lv_scr_act());
    
    /* 清理旧 timer（防止卡死） */
    if (g_teach_monitor_timer) {
        lv_timer_del(g_teach_monitor_timer);
        g_teach_monitor_timer = NULL;
    }

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "MONITOR MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    /* === 电机数据显示表格 === */
    lv_obj_t * table = lv_table_create(lv_scr_act());
    lv_table_set_col_cnt(table, 5);
    lv_table_set_row_cnt(table, 6);
    lv_obj_set_width(table, 310);
    lv_obj_align(table, LV_ALIGN_TOP_MID, 0, 70);

    /* 表头 */
    lv_table_set_cell_value(table, 0, 0, "Motor");
    lv_table_set_cell_value(table, 0, 1, "Pos");
    lv_table_set_cell_value(table, 0, 2, "Vel");
    lv_table_set_cell_value(table, 0, 3, "Torque");
    lv_table_set_cell_value(table, 0, 4, "Temp");

    /* 电机数据行（初始为零） */
    char buf[16];
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "M%d", i+1);
        lv_table_set_cell_value(table, i+1, 0, buf);
        for (int j = 1; j < 5; j++) {
            lv_table_set_cell_value(table, i+1, j, "0.0");
        }
    }

    /* 设置列宽 */
    for (int i = 0; i < 5; i++) {
        lv_table_set_col_width(table, i, 62);
    }

    /* 存储表格指针用于周期更新 */
    static lv_obj_t * g_teach_monitor_table = NULL;
    g_teach_monitor_table = table;

    /* === 定时器回调更新数据（使用全局 timer） === */
    g_teach_monitor_timer = lv_timer_create(teach_monitor_update_cb, 200, (void*)table);

    /* === BACK 按钮 === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_teach_monitor_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

/**
 * @brief 监测页面定时器回调（200ms 更新一次）
 */
static void teach_monitor_update_cb(lv_timer_t * timer) {   
    lv_obj_t * table = (lv_obj_t *)timer->user_data;
    if (!lv_obj_is_valid(table)) {
        lv_timer_del(timer);
        return;
    }

    char buf[16];
    /* 从 g_motors 读取数据并更新表格 */
    for (int i = 0; i < 5; i++) {
        /* 位置 */
        snprintf(buf, sizeof(buf), "%.2f", g_motors[i].feedback.position);
        lv_table_set_cell_value(table, i+1, 1, buf);

        /* 速度 */
        snprintf(buf, sizeof(buf), "%.2f", g_motors[i].feedback.velocity);
        lv_table_set_cell_value(table, i+1, 2, buf);

        /* 力矩 */
        snprintf(buf, sizeof(buf), "%.2f", g_motors[i].feedback.torque);
        lv_table_set_cell_value(table, i+1, 3, buf);

        /* 温度 */
        snprintf(buf, sizeof(buf), "%.1f", g_motors[i].feedback.temperature);
        lv_table_set_cell_value(table, i+1, 4, buf);
    }
}

/**
 * @brief 监测页面返回回调
 */
static void event_teach_monitor_back_cb(lv_event_t * e) {
    /* 直接清理全局 timer，防止卡死 */
    if (g_teach_monitor_timer) {
        lv_timer_del(g_teach_monitor_timer);
        g_teach_monitor_timer = NULL;
    }
    g_current_page = PAGE_TEACH_MAIN;
    create_teach_page();
}

/**
 * @brief 创建示教记录页面（示教记录，含长按支持）
 */
static void create_teach_record_page(void) {
    lv_obj_clean(lv_scr_act());

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "RECORD MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_MAIN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    /* === 动作组选择按钮（三行布局：每行2个，第3行单独放第7个） === */
    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 310, 185);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {150, 150, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {40, 40, 40, 40, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);

    for (int i = 0; i < 7; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        lv_obj_add_style(btn, &style_btn_std, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        /* 每行2个：G1-G2 第1行，G3-G4 第2行，G5-G6 第3行，G7 第4行单独 */
        int row = i / 2;
        int col = i % 2;
        if (i == 6) {
            /* G7 单独放在第4行，居中 */
            lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_STRETCH, 3, 1);
            lv_obj_set_width(btn, 150);
        } else {
            lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        }
        lv_obj_add_event_cb(btn, event_teach_group_selected_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        char buf[4];
        snprintf(buf, sizeof(buf), "G%d", i+1);
        lv_label_set_text(lv_label_create(btn), buf);
    }

    /* === BACK 按钮（直接返回TEACH主页，不弹确认框） === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_teach_record_main_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

/**
 * @brief 示教组选择回调
 */
static void event_teach_group_selected_cb(lv_event_t * e) {
    uint8_t group_idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    
    /* 进入该组的示教记录子页面 */
    create_teach_record_frames_page(group_idx);
}

/**
 * @brief 创建示教记录帧页面（为特定组记录帧）
 */
static void create_teach_record_frames_page(uint8_t group_idx) {
    lv_obj_clean(lv_scr_act());
    
    /* 清理旧的 timer（防止卡死） */
    if (g_teach_record_angle_timer) {
        lv_timer_del(g_teach_record_angle_timer);
        g_teach_record_angle_timer = NULL;
    }
    for (int i = 0; i < TEACH_FRAMES_PER_GROUP; i++) {
        if (g_frame_feedback_timers[i]) {
            lv_timer_del(g_frame_feedback_timers[i]);
            g_frame_feedback_timers[i] = NULL;
        }
    }

    char title[32];
    snprintf(title, sizeof(title), "GROUP %d", group_idx + 1);
    lv_obj_t * lbl_title = lv_label_create(lv_scr_act());
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT_MAIN, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 40);

    /* === 电机角度显示（4个关节） === */
    lv_obj_t * angle_box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(angle_box, 300, 45);
    lv_obj_align(angle_box, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_bg_color(angle_box, COL_GRAY_LIGHT, 0);
    lv_obj_set_style_border_width(angle_box, 1, 0);
    lv_obj_set_style_pad_all(angle_box, 2, 0);
    lv_obj_clear_flag(angle_box, LV_OBJ_FLAG_SCROLLABLE);

    static lv_obj_t * g_angle_labels[4] = {NULL, NULL, NULL, NULL};
    char buf[16];
    static lv_coord_t col_dsc_angle[] = {70, 70, 70, 70, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc_angle[] = {40, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(angle_box, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(angle_box, col_dsc_angle, 0);
    lv_obj_set_style_grid_row_dsc_array(angle_box, row_dsc_angle, 0);

    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "M%d\n0.0", i+1);
        g_angle_labels[i] = lv_label_create(angle_box);
        lv_label_set_text(g_angle_labels[i], buf);
        lv_obj_set_style_text_font(g_angle_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(g_angle_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_grid_cell(g_angle_labels[i], LV_GRID_ALIGN_CENTER, i, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    }

    /* === Duration 调节器 === */
    lv_obj_t * duration_box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(duration_box, 300, 45);
    lv_obj_align(duration_box, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_color(duration_box, COL_GRAY_LIGHT, 0);
    lv_obj_set_style_border_width(duration_box, 1, 0);
    lv_obj_set_flex_flow(duration_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(duration_box, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(duration_box, LV_OBJ_FLAG_SCROLLABLE);

    /* Duration - 按钮 */
    lv_obj_t * btn_dur_minus = lv_btn_create(duration_box);
    lv_obj_set_width(btn_dur_minus, 40);
    lv_obj_set_height(btn_dur_minus, 35);
    lv_obj_add_style(btn_dur_minus, &style_btn_std, 0);
    lv_label_set_text(lv_label_create(btn_dur_minus), "-");

    /* Duration 显示 */
    static lv_obj_t * g_duration_lbl = NULL;
    g_duration_lbl = lv_label_create(duration_box);
    lv_label_set_text(g_duration_lbl, "1000ms");
    lv_obj_set_style_text_font(g_duration_lbl, &lv_font_montserrat_14, 0);

    /* Duration + 按钮 */
    lv_obj_t * btn_dur_plus = lv_btn_create(duration_box);
    lv_obj_set_width(btn_dur_plus, 40);
    lv_obj_set_height(btn_dur_plus, 35);
    lv_obj_add_style(btn_dur_plus, &style_btn_std, 0);
    lv_label_set_text(lv_label_create(btn_dur_plus), "+");

    /* === 3个帧记录按钮（改为40px高度，175px cont高度，清零padding） === */
    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 310, 175);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 165);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    static teach_frame_ctx_t ctx[3];
    static uint16_t g_frame_duration = 1000;
    
    for (int i = 0; i < TEACH_FRAMES_PER_GROUP; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 40);  /* 从27改为40 */
        lv_obj_add_style(btn, &style_btn_std, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        char buf[24];
        snprintf(buf, sizeof(buf), "Frame %d", i+1);
        
        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, buf);
        lv_obj_center(lbl);

        ctx[i].group_idx = group_idx;
        ctx[i].frame_idx = i;
        ctx[i].btn_ptr = btn;  /* 保存按钮指针用于反馈 */
        ctx[i].p_duration = &g_frame_duration;  /* 新增：正确传入 duration 指针 */
        
        g_frame_btns[i] = btn;

        lv_obj_add_event_cb(btn, event_teach_frame_record_cb, LV_EVENT_CLICKED, (void*)&ctx[i]);
    }

    /* Duration +/- 按钮回调 */
    typedef struct {
        lv_obj_t * duration_lbl;
        uint16_t * p_duration;
    } duration_ctx_t;
    
    static duration_ctx_t dur_ctx;
    dur_ctx.duration_lbl = g_duration_lbl;
    dur_ctx.p_duration = &g_frame_duration;
    
    lv_obj_add_event_cb(btn_dur_minus, event_teach_frame_duration_minus_cb, LV_EVENT_CLICKED, (void*)&dur_ctx);
    lv_obj_add_event_cb(btn_dur_plus, event_teach_frame_duration_plus_cb, LV_EVENT_CLICKED, (void*)&dur_ctx);

    if (g_teach_record_angle_timer) {
        lv_timer_del(g_teach_record_angle_timer);
    }
    g_teach_record_angle_timer = lv_timer_create(teach_record_angle_update_cb, 200, (void**)g_angle_labels);

    /* === BACK 按钮 === */
    lv_obj_t * btn_back = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_back, 80, 60);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn_back, &style_btn_std, 0);
    lv_obj_set_style_bg_color(btn_back, COL_GRAY_DARK, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, event_teach_record_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);

    /* === SAVE 按钮（正中间，和 touch mode 的 START 一样） === */
    lv_obj_t * btn_save = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_save, 110, 60);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(btn_save, &style_btn_green, 0);
    lv_obj_clear_flag(btn_save, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_save, event_teach_save_config_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SAVE");
    lv_obj_center(lbl_save);
}

/**
 * @brief 角度显示定时器回调
 */
static void teach_record_angle_update_cb(lv_timer_t * timer) {
    lv_obj_t ** labels = (lv_obj_t **)timer->user_data;
    if (!labels) return;

    char buf[16];
    for (int i = 0; i < 4; i++) {
        if (labels[i] && lv_obj_is_valid(labels[i])) {
            snprintf(buf, sizeof(buf), "M%d:%.1f", i+1, g_motors[i].feedback.position);
            lv_label_set_text(labels[i], buf);
        }
    }
}

/**
 * @brief 记录帧返回回调
 */
static void event_teach_record_back_cb(lv_event_t * e){
    /* 直接清理全局 timer，防止卡死 */
    if (g_teach_record_angle_timer) {
        lv_timer_del(g_teach_record_angle_timer);
        g_teach_record_angle_timer = NULL;
    }
    g_current_page = PAGE_TEACH_RECORD;
    create_teach_record_page();
}

/**
 * @brief 帧记录点击回调（Frame3弹出Action选择，其他直接保存）
 */
static void event_teach_frame_record_cb(lv_event_t * e) {
    teach_frame_ctx_t * ctx = (teach_frame_ctx_t *)lv_event_get_user_data(e);
    uint8_t is_last = (ctx->frame_idx == TEACH_FRAMES_PER_GROUP - 1);
    uint16_t duration = ctx->p_duration ? *ctx->p_duration : 1000;

    if (is_last) {
        /* Frame3：弹出选择Action类型 */
        static const char * action_btns[] = {"Move", "Grip", "Release", ""};
        lv_obj_t * mbox = lv_msgbox_create(NULL, "Action Type", "Select action:", action_btns, false);
        lv_obj_center(mbox);
        
        static teach_frame_ctx_t saved_ctx;
        saved_ctx = *ctx;
        
        typedef struct {
            uint8_t group_idx;
            uint8_t frame_idx;
            uint16_t duration;
        } action_select_ctx_t;
        
        static action_select_ctx_t act_ctx;
        act_ctx.group_idx = saved_ctx.group_idx;
        act_ctx.frame_idx = saved_ctx.frame_idx;
        act_ctx.duration = duration;
        
        lv_obj_add_event_cb(mbox, event_teach_frame_last_action_select_cb, LV_EVENT_VALUE_CHANGED, (void*)&act_ctx);
    } else {
        /* Frame1、Frame2：直接保存，使用GROUP页面调节的Duration和默认action=0 */
        user_on_teach_save_frame(ctx->group_idx, ctx->frame_idx, duration, 0);
        
        /* 给按钮视觉反馈（变绿色） */
        if (ctx->frame_idx < 3 && g_frame_btns[ctx->frame_idx]) {
            lv_obj_t * btn = g_frame_btns[ctx->frame_idx];
            lv_obj_remove_style(btn, &style_btn_std, 0);
            lv_obj_add_style(btn, &style_btn_green, 0);
            lv_obj_t * lbl = lv_obj_get_child(btn, 0);
            if (lbl) {
                char buf[24];
                snprintf(buf, sizeof(buf), "Frame %d - Saved", ctx->frame_idx+1);
                lv_label_set_text(lbl, buf);
            }
            
            /* 2秒后恢复蓝色原样 */
            if (g_frame_feedback_timers[ctx->frame_idx]) {
                lv_timer_del(g_frame_feedback_timers[ctx->frame_idx]);
                g_frame_feedback_timers[ctx->frame_idx] = NULL;
            }
            g_frame_feedback_ctx[ctx->frame_idx].frame_idx = ctx->frame_idx;
            g_frame_feedback_ctx[ctx->frame_idx].btn = btn;
            g_frame_feedback_timers[ctx->frame_idx] = lv_timer_create(frame_feedback_timer_cb, UI_FEEDBACK_RESTORE_MS, (void*)&g_frame_feedback_ctx[ctx->frame_idx]);
        }
    }
}

/**
 * @brief RECORD 页面 BACK 回调（直接返回 TEACH 主页）
 */
static void event_teach_record_main_back_cb(lv_event_t * e) {
    g_current_page = PAGE_TEACH_MAIN;
    create_teach_page();
}

/**
 * @brief Frame Duration - 按钮回调
 */
static void event_teach_frame_duration_minus_cb(lv_event_t * e) {   
    typedef struct {
        lv_obj_t * duration_lbl;
        uint16_t * p_duration;
    } duration_ctx_t;
    
    duration_ctx_t * dur = (duration_ctx_t *)lv_event_get_user_data(e);
    if (dur && dur->p_duration && *dur->p_duration > 200) {
        *dur->p_duration -= 200;
        char buf[16];
        snprintf(buf, sizeof(buf), "%dms", *dur->p_duration);
        lv_label_set_text(dur->duration_lbl, buf);
    }
}

/**
 * @brief Frame Duration + 按钮回调
 */
static void event_teach_frame_duration_plus_cb(lv_event_t * e) {        
    typedef struct {
        lv_obj_t * duration_lbl;
        uint16_t * p_duration;
    } duration_ctx_t;
    
    duration_ctx_t * dur = (duration_ctx_t *)lv_event_get_user_data(e);
    if (dur && dur->p_duration && *dur->p_duration < 5000) {
        *dur->p_duration += 200;
        char buf[16];
        snprintf(buf, sizeof(buf), "%dms", *dur->p_duration);
        lv_label_set_text(dur->duration_lbl, buf);
    }
}

/**
 * @brief Frame3 最后一帧的 Action 选择回调
 */
static void frame_feedback_timer_cb(lv_timer_t * timer) {
    frame_feedback_ctx_t * ctx = (frame_feedback_ctx_t *)timer->user_data;
    if (ctx && ctx->btn && lv_obj_is_valid(ctx->btn)) {
        /* 恢复蓝色和原始文字 */
        lv_obj_remove_style(ctx->btn, &style_btn_green, 0);
        lv_obj_add_style(ctx->btn, &style_btn_std, 0);
        lv_obj_t * lbl = lv_obj_get_child(ctx->btn, 0);
        if (lbl) {
            char buf[24];
            snprintf(buf, sizeof(buf), "Frame %d", ctx->frame_idx+1);
            lv_label_set_text(lbl, buf);
        }
    }
    if (ctx && ctx->frame_idx < TEACH_FRAMES_PER_GROUP) {
        g_frame_feedback_timers[ctx->frame_idx] = NULL;
    }
    lv_timer_del(timer);
}

static void event_teach_frame_last_action_select_cb(lv_event_t * e) {
    typedef struct {
        uint8_t group_idx;
        uint8_t frame_idx;
        uint16_t duration;
    } action_select_ctx_t;

    lv_obj_t * mbox = lv_event_get_current_target(e);
    const char * btn_txt = lv_msgbox_get_active_btn_text(mbox);
    action_select_ctx_t * ctx = (action_select_ctx_t *)lv_event_get_user_data(e);

    uint8_t action = 0;
    if (btn_txt) {
        if (strcmp(btn_txt, "Move") == 0) action = 0;
        else if (strcmp(btn_txt, "Grip") == 0) action = 1;
        else if (strcmp(btn_txt, "Release") == 0) action = 2;

        user_on_teach_save_frame(ctx->group_idx, ctx->frame_idx, ctx->duration, action);
        
        /* Frame3 保存后也显示绿色反馈 */
        if (ctx->frame_idx < 3 && g_frame_btns[ctx->frame_idx]) {
            lv_obj_t * btn = g_frame_btns[ctx->frame_idx];
            lv_obj_remove_style(btn, &style_btn_std, 0);
            lv_obj_add_style(btn, &style_btn_green, 0);
            lv_obj_t * lbl = lv_obj_get_child(btn, 0);
            if (lbl) {
                char buf[24];
                snprintf(buf, sizeof(buf), "Frame %d - Saved", ctx->frame_idx+1);
                lv_label_set_text(lbl, buf);
            }
            
            /* 2秒后恢复蓝色原样 */
            if (g_frame_feedback_timers[ctx->frame_idx]) {
                lv_timer_del(g_frame_feedback_timers[ctx->frame_idx]);
                g_frame_feedback_timers[ctx->frame_idx] = NULL;
            }
            g_frame_feedback_ctx[ctx->frame_idx].frame_idx = ctx->frame_idx;
            g_frame_feedback_ctx[ctx->frame_idx].btn = btn;
            g_frame_feedback_timers[ctx->frame_idx] = lv_timer_create(frame_feedback_timer_cb, UI_FEEDBACK_RESTORE_MS, (void*)&g_frame_feedback_ctx[ctx->frame_idx]);
        }
    }

    lv_msgbox_close(mbox);
}

/**
 * @brief Save 反馈定时器回调（恢复按钮状态）
 */
static void save_feedback_timer_cb(lv_timer_t * t) {
    typedef struct {
        lv_obj_t * btn;
        lv_obj_t * lbl;
    } save_feedback_ctx_t;
    
    save_feedback_ctx_t * ctx = (save_feedback_ctx_t *)t->user_data;
    if (ctx->btn && lv_obj_is_valid(ctx->btn) && ctx->lbl && lv_obj_is_valid(ctx->lbl)) {
        lv_obj_remove_style(ctx->btn, &style_btn_std, 0);
        lv_obj_add_style(ctx->btn, &style_btn_green, 0);
        /* 删除内联样式的背景颜色，恢复绿色 */
        lv_obj_set_style_bg_color(ctx->btn, COL_GREEN, 0);
        lv_label_set_text(ctx->lbl, "SAVE");
    }
    lv_timer_del(t);
}

/**
 * @brief Save Config 按钮回调（保存所有帧到 Flash + 视觉反馈）
 */
static void event_teach_save_config_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_current_target(e);
    const motion_config_t * cfg = nvm_get_motion_config();
    if (cfg) {
        nvm_save_motion_config(cfg);
        LOG_D("[UI] TEACH: Save config to Flash");
        
        /* 视觉反馈：按钮变灰色+"Saved!" */
        lv_obj_t * lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_remove_style(btn, &style_btn_green, 0);
            lv_obj_add_style(btn, &style_btn_std, 0);
            lv_obj_set_style_bg_color(btn, COL_GRAY_DARK, 0);
            lv_label_set_text(lbl, "Saved!");
            
            /* 1.5秒后恢复绿色+"SAVE" */
            typedef struct {
                lv_obj_t * btn;
                lv_obj_t * lbl;
            } save_feedback_ctx_t;
            
            static save_feedback_ctx_t fb_ctx;
            fb_ctx.btn = btn;
            fb_ctx.lbl = lbl;
            
            lv_timer_create(save_feedback_timer_cb, UI_FEEDBACK_RESTORE_MS, &fb_ctx);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Weak Callbacks                                                             */
/* -------------------------------------------------------------------------- */

__attribute__((weak)) void user_on_start(void) {
    LOG_D("[UI] START triggered");
}

__attribute__((weak)) void user_on_stop(void) {
    LOG_D("[UI] STOP triggered");
}

__attribute__((weak)) void user_on_voice_start(void) {
    LOG_D("[UI] VOICE START triggered");
}

__attribute__((weak)) void user_on_voice_stop(void) {
    LOG_D("[UI] VOICE STOP triggered");
}

__attribute__((weak)) void user_on_emergency_stop(void) {
    LOG_D("[UI] EMERGENCY STOP");
}
__attribute__((weak)) void user_on_estop_reset(void) {
    LOG_D("[UI] ESTOP RESET");
}
__attribute__((weak)) void user_on_instrument_selected(const char* name) {
    LOG_D("[UI] Select %s",name);
}

__attribute__((weak)) void user_on_teach_enter(void) {
    LOG_D("[UI] TEACH ENTER");
}

__attribute__((weak)) void user_on_teach_exit(void) {
    LOG_D("[UI] TEACH EXIT");
}

__attribute__((weak)) void user_on_teach_save_frame(uint8_t group_idx, uint8_t frame_idx, uint16_t duration_ms, uint8_t action_type) {
    LOG_D("[UI] TEACH: Save frame G%d F%d, duration=%dms, action=%d", group_idx, frame_idx, duration_ms, action_type);
}
