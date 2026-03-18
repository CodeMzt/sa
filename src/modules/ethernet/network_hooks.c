/**
 * @file network_hooks.c
 * @brief FreeRTOS-IP 网络协议栈钩子函数实现
 * @date 2026-01-24
 * @author Ma Ziteng
 */

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "hal_data.h"

/**
 * @brief 网络事件钩子函数
 * @param e_network_event 事件类型
 */
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t e_network_event) {
    if (e_network_event == eNetworkUp) {
    } else if (e_network_event == eNetworkDown) {
    }
}

/**
 * @brief 随机数生成钩子函数
 * @param ul_source_ip 源IP
 * @param us_source_port 源端口
 * @param ul_destination_ip 目标IP
 * @param us_destination_port 目标端口
 * @return 随机数序列
 */
uint32_t ulApplicationGetNextSequenceNumber(uint32_t ul_source_ip,
                                            uint16_t us_source_port,
                                            uint32_t ul_destination_ip,
                                            uint16_t us_destination_port)
{
    (void)ul_source_ip;
    (void)us_source_port;
    (void)ul_destination_ip;
    (void)us_destination_port;

    uint32_t random_num = 0x55AA1234;
    g_sce_protected_on_sce.randomNumberGenerate(&random_num);

    return random_num;
}

/**
 * @brief 随机数获取钩子函数
 * @param pul_number 随机数输出指针
 * @return pdTRUE 表示成功
 */
BaseType_t xApplicationGetRandomNumber(uint32_t *pul_number) {
    *pul_number = 0x55AA1234;
    g_sce_protected_on_sce.randomNumberGenerate(pul_number);
    return pdTRUE;
}
