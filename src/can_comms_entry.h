/**
 * @file can_comms_entry.h
 * @brief 旧版 CAN 运控任务兼容头文件
 */

#ifndef CAN_COMMS_ENTRY_H_
#define CAN_COMMS_ENTRY_H_

/**
 * @brief 旧版 CAN 运控任务入口（已退役）
 * @param pvParameters FreeRTOS 任务参数
 */
void can_comms_entry(void *pvParameters);

#endif /* CAN_COMMS_ENTRY_H_ */
