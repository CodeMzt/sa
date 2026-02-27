/* ========================================================================== */
/* 修正后的暴力修复：C++ 模式实现                                             */
/* ========================================================================== */

/* 1. 必须引入这个头文件，以匹配 SDK 的声明 */
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

/* 2. 引入 FreeRTOS */
#include "voice_command.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>


__attribute__((weak)) void *ei_malloc(size_t size) {
    return pvPortMalloc(size);
}

__attribute__((weak)) void *ei_calloc(size_t nitems, size_t size) {
    size_t total_size = nitems * size;
    void *ptr = pvPortMalloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

__attribute__((weak)) void ei_free(void *ptr) {
    vPortFree(ptr);
}

/* --- 时间基准 (对接 FreeRTOS) --- */
__attribute__((weak)) uint64_t ei_read_timer_ms() {
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

__attribute__((weak)) uint64_t ei_read_timer_us() {
    return ei_read_timer_ms() * 1000;
}

/* --- 睡眠函数 --- */
__attribute__((weak)) EI_IMPULSE_ERROR ei_sleep(int32_t time_ms) {
    vTaskDelay(time_ms / portTICK_PERIOD_MS);
    return EI_IMPULSE_OK;
}

/* --- 打印输出 --- */
/* printf 本身通常是 C 链接的，但这里作为 C++ 函数实现也没问题，
   只要 ei_printf 在头文件里声明正确。 */
__attribute__((weak)) void ei_printf(const char *format, ...) {
    va_list myargs;
    va_start(myargs, format);
    //vprintf(format, myargs);
    va_end(myargs);
}

__attribute__((weak)) void ei_printf_float(float f) {
    //printf("%f", f);
}

__attribute__((weak)) char ei_getchar() { return 0; }
__attribute__((weak)) void ei_putchar(char c) { ei_printf("%c", c); }

__attribute__((weak)) EI_IMPULSE_ERROR ei_run_impulse_check_canceled() {
    return EI_IMPULSE_OK;
}
