#!/usr/bin/env python3
"""
Mock Motor Data Receiver
接收电机数据包，验证格式并打印解析结果

用于测试 mock_motor_sender.py 发送的数据包是否正确
"""

import socket
import struct
import sys

LISTEN_IP = "0.0.0.0"    # 监听所有接口
LISTEN_PORT = 2333       # 监听端口
MOTOR_COUNT = 5
PACKET_SIZE = 44


def unpack_motor_packet(packet):
    """
    解析电机数据包
    
    :param packet: 44字节的原始数据包
    :return: (valid, header, angles, torques, checksum, tail) 元组
    """
    if len(packet) != PACKET_SIZE:
        return False, None, None, None, None, None
    
    # 检查头尾
    header = struct.unpack_from('2B', packet, 0)
    tail = struct.unpack_from('B', packet, 43)[0]
    
    if header[0] != 0xA5 or header[1] != 0x5A or tail != 0xED:
        return False, header, None, None, None, tail
    
    # 解析角度
    angles = []
    for i in range(MOTOR_COUNT):
        offset = 2 + i * 4
        angle = struct.unpack_from('<f', packet, offset)[0]
        angles.append(angle)
    
    # 解析力矩
    torques = []
    for i in range(MOTOR_COUNT):
        offset = 22 + i * 4
        torque = struct.unpack_from('<f', packet, offset)[0]
        torques.append(torque)
    
    # 验证校验和
    checksum_received = struct.unpack_from('B', packet, 42)[0]
    checksum_calc = 0
    for i in range(2, 42):
        checksum_calc += packet[i]
    checksum_calc &= 0xFF
    
    is_valid = (checksum_received == checksum_calc)
    
    return is_valid, header, angles, torques, checksum_received, tail


def main():
    """主程序：监听并接收电机数据包"""
    
    print(f"Mock Motor Receiver")
    print(f"  Listen: {LISTEN_IP}:{LISTEN_PORT} (UDP)")
    print(f"  Expected Motor Count: {MOTOR_COUNT}")
    print(f"  Expected Packet Size: {PACKET_SIZE} bytes")
    print()
    
    # 创建 UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LISTEN_IP, LISTEN_PORT))
    
    print(f"Listening on {LISTEN_IP}:{LISTEN_PORT}...\n")
    
    packet_count = 0
    error_count = 0
    
    try:
        while True:
            data, addr = sock.recvfrom(1024)
            
            is_valid, header, angles, torques, checksum, tail = unpack_motor_packet(data)
            
            if not is_valid:
                error_count += 1
                print(f"[{packet_count:06d}] ERROR from {addr}: Invalid packet")
                if header and len(data) == PACKET_SIZE:
                    print(f"           Header: {header[0]:02x} {header[1]:02x}, Tail: {tail:02x}")
                else:
                    print(f"           Size: {len(data)} (expected {PACKET_SIZE})")
                continue
            
            # 打印有效包的信息（每10个包打印一次）
            if packet_count % 10 == 0:
                print(f"[{packet_count:06d}] from {addr} | ", end="")
                print("Angles: ", end="")
                for i, angle in enumerate(angles):
                    print(f"M{i+1}={angle:6.3f}", end=" ")
                print(" | ", end="")
                print("Torques: ", end="")
                for i, torque in enumerate(torques):
                    print(f"T{i+1}={torque:6.3f}", end=" ")
                print()
            
            packet_count += 1
    
    except KeyboardInterrupt:
        print(f"\n\nStopped.")
        print(f"  Total packets received: {packet_count}")
        print(f"  Error packets: {error_count}")
        if packet_count > 0:
            print(f"  Valid packets: {packet_count - error_count}")
            print(f"  Error rate: {error_count/packet_count*100:.1f}%")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
