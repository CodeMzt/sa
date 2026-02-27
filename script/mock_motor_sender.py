#!/usr/bin/env python3
"""
Mock Motor Data Sender
模拟单片机发送5个电机状态数据包到PC

数据包格式 (motor_packet_t)：
  [0]     0xA5          header[0]
  [1]     0x5A          header[1]
  [2-21]  float×5       angles[5]   (4关节+1夹爪)
  [22-41] float×5       torques[5]  (4关节+1夹爪)
  [42]    uint8         checksum    (字节2~41累加和)
  [43]    0xED          tail
  总长44字节
"""

import socket
import struct
import math
import time
import sys

# ============================================================
# 协议配置
# ============================================================
TARGET_IP = "192.168.31.191"  # PC IP (接收端)
TARGET_PORT = 2333            # UDP 端口
SOURCE_IP = "192.168.31.100"  # 单片机 IP (模拟源)
SEND_INTERVAL = 0.05          # 50ms = 20Hz

MOTOR_COUNT = 5
PACKET_SIZE = 44

# ============================================================
# 电机动作参数
# ============================================================
AMPLITUDE = 1.0       # 角度振幅 rad
FREQUENCY = 0.5       # 频率 Hz
TORQUE_BASE = 0.5     # 力矩基准 Nm
TORQUE_NOISE = 0.2    # 力矩噪声


def pack_motor_packet(angles, torques):
    """
    打包电机数据成 motor_packet_t 格式
    
    :param angles: list of 5 floats (关节1-4角度 + 夹爪角度)
    :param torques: list of 5 floats (关节1-4力矩 + 夹爪力矩)
    :return: 44字节的二进制数据包
    """
    if len(angles) != MOTOR_COUNT or len(torques) != MOTOR_COUNT:
        raise ValueError(f"Expected {MOTOR_COUNT} motors, got angles={len(angles)}, torques={len(torques)}")
    
    # 初始化包
    packet = bytearray(PACKET_SIZE)
    packet[0] = 0xA5
    packet[1] = 0x5A
    
    # 打包角度（5个 float，小端序）
    for i in range(MOTOR_COUNT):
        offset = 2 + i * 4
        struct.pack_into('<f', packet, offset, angles[i])
    
    # 打包力矩（5个 float，小端序）
    for i in range(MOTOR_COUNT):
        offset = 22 + i * 4
        struct.pack_into('<f', packet, offset, torques[i])
    
    # 计算校验和（字节2到41，共40字节）
    checksum = 0
    for i in range(2, 42):
        checksum += packet[i]
    checksum &= 0xFF  # 只取低8位
    packet[42] = checksum
    
    packet[43] = 0xED
    
    return bytes(packet)


def generate_motor_states(t):
    """
    生成正弦波动作（5个电机，相位差 72°）
    
    :param t: 时间戳 (秒)
    :return: (angles, torques) 元组
    """
    angles = []
    torques = []
    
    for i in range(MOTOR_COUNT):
        # 相位差 = i * (2π/5) = i * 72°
        phase = (2 * math.pi * i) / MOTOR_COUNT
        
        # 角度：正弦波振动
        angle = AMPLITUDE * math.sin(2 * math.pi * FREQUENCY * t + phase)
        angles.append(angle)
        
        # 力矩：基准值 + 小幅随机噪声（由正弦波生成，保证可重复）
        torque = TORQUE_BASE + TORQUE_NOISE * math.sin(2 * math.pi * FREQUENCY * t * 2 + phase)
        torques.append(torque)
    
    return angles, torques


def main():
    """主程序：循环发送电机数据包"""
    
    print(f"Mock Motor Sender")
    print(f"  Target: {TARGET_IP}:{TARGET_PORT} (UDP)")
    print(f"  Send Interval: {SEND_INTERVAL*1000:.0f} ms ({1/SEND_INTERVAL:.0f} Hz)")
    print(f"  Motor Count: {MOTOR_COUNT}")
    print(f"  Packet Size: {PACKET_SIZE} bytes")
    print()
    
    # 创建 UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        t_start = time.time()
        packet_count = 0
        
        while True:
            t = time.time() - t_start
            
            # 生成电机状态
            angles, torques = generate_motor_states(t)
            
            # 打包数据
            packet = pack_motor_packet(angles, torques)
            
            # 发送
            sock.sendto(packet, (TARGET_IP, TARGET_PORT))
            
            # 打印调试信息（每10个包打印一次）
            if packet_count % 10 == 0:
                print(f"[{packet_count:06d}] t={t:6.2f}s | ", end="")
                print("Angles: ", end="")
                for i, angle in enumerate(angles):
                    print(f"M{i+1}={angle:6.3f}", end=" ")
                print()
            
            packet_count += 1
            time.sleep(SEND_INTERVAL)
    
    except KeyboardInterrupt:
        print(f"\n\nStopped. Total packets sent: {packet_count}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
