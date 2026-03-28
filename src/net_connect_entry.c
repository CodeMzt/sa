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
#if !defined(UDP_TEST_MODE)
#include "nvm_manager.h"
#endif
#include <string.h>

static const uint8_t dns_server_addr[4] = { 8, 8, 8, 8 };

extern uint8_t  g_ether0_mac_address[6];
static struct freertos_sockaddr server_addr;
static uint8_t send_buf[PACKET_SIZE];

#ifndef ETH_USE_FIXED_CONFIG
#define ETH_USE_FIXED_CONFIG 1
#endif

#if !defined(UDP_TEST_MODE) && ETH_USE_FIXED_CONFIG
static const uint8_t k_eth_fixed_ip[4]      = { 192U, 168U, 31U, 250U };
static const uint8_t k_eth_fixed_netmask[4] = { 255U, 255U, 255U, 0U };
static const uint8_t k_eth_fixed_gateway[4] = { 192U, 168U, 31U, 1U };
static const uint8_t k_eth_fixed_server_ip[4] = { 192U, 168U, 31U, 229U };
static const uint16_t k_eth_fixed_server_port = 2333U;
#endif

static BaseType_t net_connect_init(const uint8_t ip_addr[4],
                                   const uint8_t net_mask[4],
                                   const uint8_t gateway[4],
                                   const uint8_t mac_addr[6]);

#if defined(UDP_TEST_MODE)
#ifndef UDP_TEST_LOCAL_IP0
#define UDP_TEST_LOCAL_IP0      192U
#define UDP_TEST_LOCAL_IP1      168U
#define UDP_TEST_LOCAL_IP2      3U
#define UDP_TEST_LOCAL_IP3      100U
#endif

#ifndef UDP_TEST_NETMASK0
#define UDP_TEST_NETMASK0       255U
#define UDP_TEST_NETMASK1       255U
#define UDP_TEST_NETMASK2       255U
#define UDP_TEST_NETMASK3       0U
#endif

#ifndef UDP_TEST_GATEWAY0
#define UDP_TEST_GATEWAY0       192U
#define UDP_TEST_GATEWAY1       168U
#define UDP_TEST_GATEWAY2       3U
#define UDP_TEST_GATEWAY3       1U
#endif

#ifndef UDP_TEST_SERVER_IP0
#define UDP_TEST_SERVER_IP0     192U
#define UDP_TEST_SERVER_IP1     168U
#define UDP_TEST_SERVER_IP2     3U
#define UDP_TEST_SERVER_IP3     200U
#endif

#ifndef UDP_TEST_SERVER_PORT
#define UDP_TEST_SERVER_PORT    2333U
#endif

static const uint8_t udp_test_local_ip[4] = {
    UDP_TEST_LOCAL_IP0, UDP_TEST_LOCAL_IP1, UDP_TEST_LOCAL_IP2, UDP_TEST_LOCAL_IP3
};

static const uint8_t udp_test_netmask[4] = {
    UDP_TEST_NETMASK0, UDP_TEST_NETMASK1, UDP_TEST_NETMASK2, UDP_TEST_NETMASK3
};

static const uint8_t udp_test_gateway[4] = {
    UDP_TEST_GATEWAY0, UDP_TEST_GATEWAY1, UDP_TEST_GATEWAY2, UDP_TEST_GATEWAY3
};

static const uint8_t udp_test_server_ip[4] = {
    UDP_TEST_SERVER_IP0, UDP_TEST_SERVER_IP1, UDP_TEST_SERVER_IP2, UDP_TEST_SERVER_IP3
};
#endif

#if !defined(UDP_TEST_MODE)
static bool is_valid_mac(const uint8_t mac[6]) {
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0U) all_zero = false;
        if (mac[i] != 0xFFU) all_ff = false;
    }
    return !(all_zero || all_ff);
}
#endif

void net_connect_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED (pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_NET_CONNECT
    LOG_I("Test mode: net_connect thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    vTaskDelay(pdMS_TO_TICKS(3000));
    LOG_I("Ethernet connect thread started.");

#if defined(UDP_TEST_MODE)
    if (net_connect_init(udp_test_local_ip, udp_test_netmask, udp_test_gateway, g_ether0_mac_address) != pdTRUE) {
        LOG_E("Failed to start the Ethernet.");
    }

    server_addr.sin_family = FREERTOS_AF_INET;
    server_addr.sin_port = FreeRTOS_htons((uint16_t)UDP_TEST_SERVER_PORT);
    server_addr.sin_address = (IP_Address_t)FreeRTOS_inet_addr_quick(udp_test_server_ip[0],
                                                       udp_test_server_ip[1],
                                                       udp_test_server_ip[2],
                                                       udp_test_server_ip[3]);

    LOG_I("UDP test target: %u.%u.%u.%u:%u",
          udp_test_server_ip[0], udp_test_server_ip[1], udp_test_server_ip[2], udp_test_server_ip[3],
          (unsigned int)UDP_TEST_SERVER_PORT);

#else

    uint8_t ip_addr[4] = {0};
    uint8_t net_mask[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t server_ip[4] = {0};
    uint8_t mac_addr[6] = {0};
    uint16_t server_port = 0U;

    fsp_err_t nvm_err = nvm_init();
    if (nvm_err != FSP_SUCCESS) {
        LOG_E("NVM init failed in net task: %d", nvm_err);
        g_sys_status.is_eth_connected = false;
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    const sys_config_t *cfg = nvm_get_sys_config();

#if ETH_USE_FIXED_CONFIG
    memcpy(ip_addr, k_eth_fixed_ip, sizeof(ip_addr));
    memcpy(net_mask, k_eth_fixed_netmask, sizeof(net_mask));
    memcpy(gateway, k_eth_fixed_gateway, sizeof(gateway));
    memcpy(server_ip, k_eth_fixed_server_ip, sizeof(server_ip));
    server_port = k_eth_fixed_server_port;
#else
    ip_addr[0] = cfg->ip_addr[0];
    ip_addr[1] = cfg->ip_addr[1];
    ip_addr[2] = cfg->ip_addr[2];
    ip_addr[3] = cfg->ip_addr[3];

    net_mask[0] = cfg->netmask[0];
    net_mask[1] = cfg->netmask[1];
    net_mask[2] = cfg->netmask[2];
    net_mask[3] = cfg->netmask[3];

    gateway[0] = cfg->gateway[0];
    gateway[1] = cfg->gateway[1];
    gateway[2] = cfg->gateway[2];
    gateway[3] = cfg->gateway[3];

    server_ip[0] = cfg->server_ip[0];
    server_ip[1] = cfg->server_ip[1];
    server_ip[2] = cfg->server_ip[2];
    server_ip[3] = cfg->server_ip[3];
    server_port = cfg->server_port;
#endif

    if (is_valid_mac(cfg->mac_addr)) {
        memcpy(mac_addr, cfg->mac_addr, sizeof(mac_addr));
    } else {
        memcpy(mac_addr, g_ether0_mac_address, sizeof(mac_addr));
    }

    LOG_I("ETH cfg: ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u server=%u.%u.%u.%u:%u",
          ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
          net_mask[0], net_mask[1], net_mask[2], net_mask[3],
          gateway[0], gateway[1], gateway[2], gateway[3],
          server_ip[0], server_ip[1], server_ip[2], server_ip[3],
          server_port);

    if (net_connect_init(ip_addr, net_mask, gateway, mac_addr) != pdTRUE) {
        LOG_E("Failed to start the Ethernet.");
    }

    server_addr.sin_family = FREERTOS_AF_INET;
    server_addr.sin_port = FreeRTOS_htons(server_port);
    server_addr.sin_address = (IP_Address_t)FreeRTOS_inet_addr_quick(server_ip[0],
                                                       server_ip[1],
                                                       server_ip[2],
                                                       server_ip[3]);
#endif

    while (1) {
        /* Link state is managed by FreeRTOS+TCP internal tasks; avoid direct PHY polling here. */
        BaseType_t net_up = FreeRTOS_IsNetworkUp();
        static bool s_logged_not_ready = false;

        if (net_up == pdTRUE) {
            g_sys_status.is_eth_connected = true;
            s_logged_not_ready = false;

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
            if (!s_logged_not_ready) {
                LOG_W("Ethernet not ready: net_up=%ld", (long)net_up);
                s_logged_not_ready = true;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief 以太网相关初始化
 * @return pdTRUE 表示成功
 */
static BaseType_t net_connect_init(const uint8_t ip_addr[4],
                                   const uint8_t net_mask[4],
                                   const uint8_t gateway[4],
                                   const uint8_t mac_addr[6]) {
    fsp_err_t err = g_sce_protected_on_sce.open(&sce_ctrl,&sce_cfg);
    if (err != FSP_SUCCESS) return pdFALSE;

    BaseType_t connect_status = FreeRTOS_IPInit(ip_addr, net_mask, gateway,
                    dns_server_addr, mac_addr);

    return connect_status;
}
