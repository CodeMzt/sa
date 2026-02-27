#include <net_connect.h>
#include "packet_packer.h"
#include "sys_log.h"
#include "shared_data.h"

static const uint8_t uc_ip_address[4] = { 192, 168, 31, 100 };
static const uint8_t uc_net_mask[4]   = { 255, 255, 255, 0 };
static const uint8_t uc_gateway_address[4] = { 192, 168, 31, 1 };
static const uint8_t uc_dns_server_address[4] = { 8, 8, 8, 8 };

extern uint8_t  g_ether0_mac_address[6];
static struct freertos_sockaddr server_addr;
static uint8_t send_buf[PACKET_SIZE];

static BaseType_t net_connect_init(void);

void net_connect_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED (pvParameters);
    LOG_I("Ethernet connect thread started.");
    if(net_connect_init()!=pdTRUE) {
        LOG_E("Failed to start the Ethernet.");
    }

    server_addr.sin_family = FREERTOS_AF_INET;
    server_addr.sin_port = FreeRTOS_htons(2333);
    server_addr.sin_address = (IP_Address_t)FreeRTOS_inet_addr("192.168.31.250");

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
    fsp_err_t err = g_sce_protected_on_sce.open(&sce_ctrl,&sce_cfg);
    if(err != FSP_SUCCESS) return pdFALSE;

    BaseType_t connect_status = FreeRTOS_IPInit(uc_ip_address, uc_net_mask, uc_gateway_address,
                    uc_dns_server_address, g_ether0_mac_address);

    return connect_status;
}
