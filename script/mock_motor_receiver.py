#!/usr/bin/env python3
"""
Mock Motor Data Receiver

Receive and validate a UDP packet that contains 6 servo angles and torques.
"""

import socket
import struct

LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 2333

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


def unpack_motor_packet(packet):
    """Unpack and verify a 6-servo packet."""
    if len(packet) != PACKET_SIZE:
        return False, None, None, None, None, None

    header = struct.unpack_from("2B", packet, 0)
    tail = struct.unpack_from("B", packet, TAIL_OFFSET)[0]

    if header[0] != 0xA5 or header[1] != 0x5A or tail != 0xED:
        return False, header, None, None, None, tail

    angles = []
    for i in range(MOTOR_COUNT):
        angle = struct.unpack_from("<f", packet, ANGLES_OFFSET + i * FLOAT_SIZE)[0]
        angles.append(angle)

    torques = []
    for i in range(MOTOR_COUNT):
        torque = struct.unpack_from("<f", packet, TORQUES_OFFSET + i * FLOAT_SIZE)[0]
        torques.append(torque)

    checksum_received = struct.unpack_from("B", packet, CHECKSUM_OFFSET)[0]
    checksum_calc = sum(packet[ANGLES_OFFSET:CHECKSUM_OFFSET]) & 0xFF
    is_valid = checksum_received == checksum_calc

    return is_valid, header, angles, torques, checksum_received, tail


def main():
    print("Mock Motor Receiver")
    print(f"  Listen: {LISTEN_IP}:{LISTEN_PORT} (UDP)")
    print(f"  Expected Motor Count: {MOTOR_COUNT}")
    print(f"  Expected Packet Size: {PACKET_SIZE} bytes")
    print()

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
                    print(f"           Checksum: recv={checksum}, calc={sum(data[ANGLES_OFFSET:CHECKSUM_OFFSET]) & 0xFF}")
                else:
                    print(f"           Size: {len(data)} (expected {PACKET_SIZE})")
                continue

            if packet_count % 10 == 0:
                print(f"[{packet_count:06d}] from {addr} | ", end="")
                print("Angles: ", end="")
                for i, angle in enumerate(angles):
                    print(f"M{i + 1}={angle:6.3f}", end=" ")
                print(" | ", end="")
                print("Torques: ", end="")
                for i, torque in enumerate(torques):
                    print(f"T{i + 1}={torque:6.3f}", end=" ")
                print()

            packet_count += 1

    except KeyboardInterrupt:
        print("\n\nStopped.")
        print(f"  Total packets received: {packet_count}")
        print(f"  Error packets: {error_count}")
        if packet_count > 0:
            print(f"  Valid packets: {packet_count - error_count}")
            print(f"  Error rate: {error_count / packet_count * 100:.1f}%")
    except Exception as exc:
        print(f"Error: {exc}")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
