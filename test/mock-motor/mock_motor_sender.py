#!/usr/bin/env python3
"""
Mock Motor Data Sender
Send a UDP packet that contains 6 servo angles and torques.

Packet layout:
  [0]      0xA5        header[0]
  [1]      0x5A        header[1]
  [2-25]   float x 6   angles[6]
  [26-49]  float x 6   torques[6]
  [50]     uint8       checksum (sum of bytes 2-49)
  [51]     0xED        tail
"""

import math
import socket
import struct
import time

# Protocol config
TARGET_IP = "192.168.31.191"
TARGET_PORT = 2333
SOURCE_IP = "192.168.31.100"
SEND_INTERVAL = 0.05  # 50 ms = 20 Hz

MOTOR_COUNT = 6
HEADER_SIZE = 2
FLOAT_SIZE = 4
CHECKSUM_SIZE = 1
TAIL_SIZE = 1
ANGLES_OFFSET = HEADER_SIZE
TORQUES_OFFSET = ANGLES_OFFSET + MOTOR_COUNT * FLOAT_SIZE
CHECKSUM_OFFSET = TORQUES_OFFSET + MOTOR_COUNT * FLOAT_SIZE
TAIL_OFFSET = CHECKSUM_OFFSET + CHECKSUM_SIZE
PACKET_SIZE = TAIL_OFFSET + TAIL_SIZE

# Motion profile
AMPLITUDE = 1.0
FREQUENCY = 0.5
TORQUE_BASE = 0.5
TORQUE_NOISE = 0.2


def pack_motor_packet(angles, torques):
    """Pack 6-servo state into the UDP payload."""
    if len(angles) != MOTOR_COUNT or len(torques) != MOTOR_COUNT:
        raise ValueError(
            f"Expected {MOTOR_COUNT} motors, got angles={len(angles)}, torques={len(torques)}"
        )

    packet = bytearray(PACKET_SIZE)
    packet[0] = 0xA5
    packet[1] = 0x5A

    for i, angle in enumerate(angles):
        struct.pack_into("<f", packet, ANGLES_OFFSET + i * FLOAT_SIZE, angle)

    for i, torque in enumerate(torques):
        struct.pack_into("<f", packet, TORQUES_OFFSET + i * FLOAT_SIZE, torque)

    checksum = sum(packet[ANGLES_OFFSET:CHECKSUM_OFFSET]) & 0xFF
    packet[CHECKSUM_OFFSET] = checksum
    packet[TAIL_OFFSET] = 0xED

    return bytes(packet)


def generate_motor_states(t):
    """Generate 6 servo angles/torques with evenly spaced phase offsets."""
    angles = []
    torques = []

    for i in range(MOTOR_COUNT):
        phase = (2 * math.pi * i) / MOTOR_COUNT
        angle = AMPLITUDE * math.sin(2 * math.pi * FREQUENCY * t + phase)
        torque = TORQUE_BASE + TORQUE_NOISE * math.sin(2 * math.pi * FREQUENCY * t * 2 + phase)
        angles.append(angle)
        torques.append(torque)

    return angles, torques


def main():
    print("Mock Motor Sender")
    print(f"  Target: {TARGET_IP}:{TARGET_PORT} (UDP)")
    print(f"  Send Interval: {SEND_INTERVAL * 1000:.0f} ms ({1 / SEND_INTERVAL:.0f} Hz)")
    print(f"  Motor Count: {MOTOR_COUNT}")
    print(f"  Packet Size: {PACKET_SIZE} bytes")
    print()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        t_start = time.time()
        packet_count = 0

        while True:
            t = time.time() - t_start
            angles, torques = generate_motor_states(t)
            packet = pack_motor_packet(angles, torques)
            sock.sendto(packet, (TARGET_IP, TARGET_PORT))

            if packet_count % 10 == 0:
                print(f"[{packet_count:06d}] t={t:6.2f}s | ", end="")
                print("Angles: ", end="")
                for i, angle in enumerate(angles):
                    print(f"M{i + 1}={angle:6.3f}", end=" ")
                print()

            packet_count += 1
            time.sleep(SEND_INTERVAL)

    except KeyboardInterrupt:
        print(f"\n\nStopped. Total packets sent: {packet_count}")
    except Exception as exc:
        print(f"Error: {exc}")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
