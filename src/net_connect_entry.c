#include <net_connect.h>
#include "packet_packer.h"
#include "sys_log.h"
#include "shared_data.h"
#include "nvm_manager.h"
#include <string.h>

static const uint8_t uc_dns_server_address[4] = { 8, 8, 8, 8 };

extern uint8_t  g_ether0_mac_address[6];
static struct freertos_sockaddr server_addr;
static uint8_t send_buf[PACKET_SIZE];

static BaseType_t net_connect_init(void);

static bool is_valid_mac(const uint8_t mac[6]) {
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0U) {
            all_zero = false;
        }
        if (mac[i] != 0xFFU) {
            all_ff = false;
        }
    }
    return !(all_zero || all_ff);
}

void net_connect_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED (pvParameters);
    vTaskDelay(pdMS_TO_TICKS(3000));
    LOG_I("Ethernet connect thread started.");

    const sys_config_t *cfg = nvm_get_sys_config();

    if(net_connect_init()!=pdTRUE) {
        LOG_E("Failed to start the Ethernet.");
    }

    server_addr.sin_family = FREERTOS_AF_INET;
    server_addr.sin_port = FreeRTOS_htons(cfg->server_port);
    server_addr.sin_address = (IP_Address_t)FreeRTOS_inet_addr_quick(cfg->server_ip[0],
                                                       cfg->server_ip[1],
                                                       cfg->server_ip[2],
                                                       cfg->server_ip[3]);

    while (1) {
        if(g_ether0.p_cfg->p_ether_phy_instance->p_api->linkStatusGet(g_ether0.p_cfg->p_ether_phy_instance->p_ctrl) == FSP_SUCCESS){
            LOG_I("Successfully connect to the Ethernet.");
            g_sys_status.is_eth_connected = true;
            Socket_t udp_sock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);

            if (udp_sock != FREERTOS_INVALID_SOCKET){
                LOG_I("Start sending to the server...");
                while (1){
                    pack_motor_data(send_buf);
                    FreeRTOS_sendto(udp_sock, send_buf, PACKET_SIZE, 0, &server_addr, sizeof(server_addr));
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            else LOG_W("Failed to connect to the server.");
            FreeRTOS_closesocket(udp_sock);
        }
        LOG_W("Failed to connect to the Ethernet.");
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    if(err != FSP_SUCCESS) return pdFALSE;

    BaseType_t connect_status = FreeRTOS_IPInit(ip_addr, net_mask, gateway,
                    uc_dns_server_address, mac_addr);

    return connect_status;
}
