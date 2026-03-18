/**
 * @file  net_connect_entry.c
 * @brief 以太网连接任务入口（初始化以太网 + NVM，循环发送 UDP 数据包）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#include <net_connect.h>
#include "packet_packer.h"
#include "sys_log.h"
#include "shared_data.h"
#include "test_mode.h"
#include "nvm_manager.h"
#include <string.h>

static const uint8_t dns_server_addr[4] = { 8, 8, 8, 8 };

extern uint8_t  g_ether0_mac_address[6];
static struct freertos_sockaddr server_addr;
static uint8_t send_buf[PACKET_SIZE];

static BaseType_t net_connect_init(void);

static bool is_valid_mac(const uint8_t mac[6]) {
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0U) all_zero = false;
        if (mac[i] != 0xFFU) all_ff = false;
    }
    return !(all_zero || all_ff);
}

void net_connect_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED (pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_NET_CONNECT
    LOG_I("Test mode: net_connect thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    vTaskDelay(pdMS_TO_TICKS(3000));
    LOG_I("Ethernet connect thread started.");

    fsp_err_t nvm_err = nvm_init();
    if (nvm_err != FSP_SUCCESS) {
        LOG_E("NVM init failed in net task: %d", nvm_err);
        g_sys_status.is_eth_connected = false;
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    const sys_config_t *cfg = nvm_get_sys_config();

    LOG_I("ETH cfg: ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u server=%u.%u.%u.%u:%u",
          cfg->ip_addr[0], cfg->ip_addr[1], cfg->ip_addr[2], cfg->ip_addr[3],
          cfg->netmask[0], cfg->netmask[1], cfg->netmask[2], cfg->netmask[3],
          cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3],
          cfg->server_ip[0], cfg->server_ip[1], cfg->server_ip[2], cfg->server_ip[3],
          cfg->server_port);

    if (net_connect_init() != pdTRUE) LOG_E("Failed to start the Ethernet.");

    server_addr.sin_family = FREERTOS_AF_INET;
    server_addr.sin_port = FreeRTOS_htons(cfg->server_port);
    server_addr.sin_address = (IP_Address_t)FreeRTOS_inet_addr_quick(cfg->server_ip[0],
                                                       cfg->server_ip[1],
                                                       cfg->server_ip[2],
                                                       cfg->server_ip[3]);

    while (1) {
        fsp_err_t link_err = g_ether0.p_cfg->p_ether_phy_instance->p_api->linkStatusGet(
                g_ether0.p_cfg->p_ether_phy_instance->p_ctrl);
        BaseType_t net_up = FreeRTOS_IsNetworkUp();

        if ((link_err == FSP_SUCCESS) && (net_up == pdTRUE)) {
            g_sys_status.is_eth_connected = true;

            Socket_t udp_sock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);
            if (udp_sock == FREERTOS_INVALID_SOCKET) {
                LOG_W("UDP socket create failed.");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            LOG_I("Ethernet UP, start UDP send loop.");
            while (FreeRTOS_IsNetworkUp() == pdTRUE) {
                pack_motor_data(send_buf);
                BaseType_t sent = FreeRTOS_sendto(udp_sock, send_buf, PACKET_SIZE, 0, &server_addr, sizeof(server_addr));
                if (sent < 0) {
                    LOG_W("UDP send failed: %ld", (long)sent);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            FreeRTOS_closesocket(udp_sock);
            g_sys_status.is_eth_connected = false;
            LOG_W("Ethernet down or stack down, UDP loop stopped.");
        } else {
            g_sys_status.is_eth_connected = false;
            LOG_W("Ethernet not ready: link_err=%d net_up=%ld", link_err, (long)net_up);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief 以太网相关初始化
 * @return pdTRUE 表示成功
 */
static BaseType_t net_connect_init(void){
    const sys_config_t *cfg = nvm_get_sys_config();

    uint8_t ip_addr[4] = { cfg->ip_addr[0], cfg->ip_addr[1], cfg->ip_addr[2], cfg->ip_addr[3] };
    uint8_t net_mask[4] = { cfg->netmask[0], cfg->netmask[1], cfg->netmask[2], cfg->netmask[3] };
    uint8_t gateway[4] = { cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3] };
    uint8_t mac_addr[6];

    if (is_valid_mac(cfg->mac_addr)) {
        memcpy(mac_addr, cfg->mac_addr, sizeof(mac_addr));
    } else {
        memcpy(mac_addr, g_ether0_mac_address, sizeof(mac_addr));
    }

    fsp_err_t err = g_sce_protected_on_sce.open(&sce_ctrl,&sce_cfg);
    if (err != FSP_SUCCESS) return pdFALSE;

    BaseType_t connect_status = FreeRTOS_IPInit(ip_addr, net_mask, gateway,
                    dns_server_addr, mac_addr);

    return connect_status;
}
