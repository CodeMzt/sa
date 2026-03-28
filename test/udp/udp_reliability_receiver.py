#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UDP 可靠性测试接收端（固定周期遥测包）。

方法说明：
1) 保持原始 44 字节协议不变。
2) 以首个有效包作为时间原点。
3) 按固定发送周期（默认 50ms）建立期望时间轴。
4) 计算逐包时延与抖动。
5) 用期望包数和有效包数估算丢包率。
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import socket
import struct
import time
from dataclasses import dataclass
from statistics import mean
from typing import List, Optional, Tuple

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


@dataclass
class PacketStats:
    recv_index: int
    recv_time_s: float
    expected_time_s: float
    delay_ms: float
    jitter_ms: float
    source_ip: str
    source_port: int


def verify_packet(packet: bytes) -> bool:
    if len(packet) != PACKET_SIZE:
        return False

    header0, header1 = struct.unpack_from("2B", packet, 0)
    tail = struct.unpack_from("B", packet, TAIL_OFFSET)[0]

    if header0 != 0xA5 or header1 != 0x5A or tail != 0xED:
        return False

    checksum_received = struct.unpack_from("B", packet, CHECKSUM_OFFSET)[0]
    checksum_calc = sum(packet[ANGLES_OFFSET:CHECKSUM_OFFSET]) & 0xFF
    return checksum_received == checksum_calc


def percentile(values: List[float], p: float) -> Optional[float]:
    if not values:
        return None
    if p <= 0:
        return min(values)
    if p >= 100:
        return max(values)

    sorted_vals = sorted(values)
    rank = (len(sorted_vals) - 1) * (p / 100.0)
    low = int(math.floor(rank))
    high = int(math.ceil(rank))
    if low == high:
        return sorted_vals[low]
    frac = rank - low
    return sorted_vals[low] * (1.0 - frac) + sorted_vals[high] * frac


def fmt_ms(v: Optional[float]) -> str:
    if v is None:
        return "无"
    return f"{v:.3f}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="UDP 可靠性测试接收端")
    parser.add_argument("--listen-ip", default="0.0.0.0", help="监听 IP（默认：0.0.0.0）")
    parser.add_argument("--port", type=int, default=2333, help="UDP 端口（默认：2333）")
    parser.add_argument("--interval-ms", type=float, default=50.0, help="期望发送周期，单位毫秒（默认：50）")
    parser.add_argument("--duration-s", type=float, default=1800.0, help="测试时长（从首个有效包开始计时，默认：1800 秒即 30 分钟）；0 表示直到手动 Ctrl+C 停止")
    parser.add_argument("--print-every", type=int, default=200, help="每收到 N 个有效包打印一次进度")
    parser.add_argument("--csv", default="", help="CSV 输出路径（可选）")
    return parser.parse_args()


def prepare_csv_output_path(csv_path: str) -> Path:
    """解析 CSV 输出路径，并确保父目录存在。"""
    path = Path(csv_path).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def main() -> None:
    args = parse_args()
    interval_s = args.interval_ms / 1000.0

    print("UDP 可靠性测试接收端")
    print(f"  监听地址: {args.listen_ip}:{args.port}")
    print(f"  数据包长度: {PACKET_SIZE} 字节")
    print(f"  期望发送周期: {args.interval_ms:.3f} ms")
    if args.duration_s > 0:
        print(f"  测试时长: {args.duration_s:.1f} s（从首个有效包开始）")
    if args.csv:
        print(f"  CSV 输出: {args.csv}")
    print()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_ip, args.port))
    sock.settimeout(1.0)

    first_valid_ts: Optional[float] = None
    last_valid_ts: Optional[float] = None
    last_delay_ms: Optional[float] = None

    raw_recv = 0
    valid_recv = 0
    invalid_recv = 0

    delay_ms_list: List[float] = []
    jitter_ms_list: List[float] = []
    records: List[PacketStats] = []

    print("等待 UDP 数据包中... 按 Ctrl+C 可提前停止。\n")

    try:
        while True:
            now = time.perf_counter()
            if first_valid_ts is not None and args.duration_s > 0 and (now - first_valid_ts) >= args.duration_s:
                break

            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue

            rx_ts = time.perf_counter()
            raw_recv += 1

            if not verify_packet(data):
                invalid_recv += 1
                continue

            if first_valid_ts is None:
                first_valid_ts = rx_ts
                print(f"收到首个有效包，来源 {addr[0]}:{addr[1]}")

            # 以有效包接收顺序作为索引。
            recv_index = valid_recv
            expected_ts = first_valid_ts + recv_index * interval_s
            delay_ms = (rx_ts - expected_ts) * 1000.0

            if last_delay_ms is None:
                jitter_ms = 0.0
            else:
                jitter_ms = abs(delay_ms - last_delay_ms)
                jitter_ms_list.append(jitter_ms)

            stat = PacketStats(
                recv_index=recv_index,
                recv_time_s=rx_ts,
                expected_time_s=expected_ts,
                delay_ms=delay_ms,
                jitter_ms=jitter_ms,
                source_ip=addr[0],
                source_port=addr[1],
            )
            records.append(stat)

            delay_ms_list.append(delay_ms)
            last_delay_ms = delay_ms
            last_valid_ts = rx_ts
            valid_recv += 1

            if args.print_every > 0 and (valid_recv % args.print_every == 0):
                print(
                    f"[有效包={valid_recv}] 时延ms(均值={mean(delay_ms_list):.3f}, "
                    f"P95={fmt_ms(percentile(delay_ms_list, 95))}, 最大={max(delay_ms_list):.3f}), "
                    f"抖动ms(均值={mean(jitter_ms_list) if jitter_ms_list else 0.0:.3f})"
                )

    except KeyboardInterrupt:
        print("\n已由用户中断。")
    finally:
        sock.close()

    print("\n===== UDP 测试汇总 =====")
    print(f"收到原始包数   : {raw_recv}")
    print(f"收到有效包数   : {valid_recv}")
    print(f"收到无效包数   : {invalid_recv}")

    if valid_recv == 0 or first_valid_ts is None or last_valid_ts is None:
        print("未捕获到有效数据包，请检查网络连通性与发送端配置。")
        return

    elapsed_s = max(last_valid_ts - first_valid_ts, 0.0)

    # 固定时长测试使用配置时长作为期望窗口，避免停止边界导致 +1 偏差。
    if args.duration_s > 0:
        expected_count = max(int(math.floor(args.duration_s / interval_s)), 1)
    else:
        expected_count = int(math.floor(elapsed_s / interval_s)) + 1

    loss_count = max(expected_count - valid_recv, 0)
    loss_rate = (loss_count / expected_count * 100.0) if expected_count > 0 else 0.0
    invalid_rate = (invalid_recv / raw_recv * 100.0) if raw_recv > 0 else 0.0
    actual_rate_hz = (valid_recv / elapsed_s) if elapsed_s > 0 else 0.0
    effective_interval_ms = ((elapsed_s / (valid_recv - 1)) * 1000.0) if valid_recv > 1 else None

    delay_min_ms = min(delay_ms_list)
    delay_avg_ms = mean(delay_ms_list)
    delay_p50_ms = percentile(delay_ms_list, 50)
    delay_p95_ms = percentile(delay_ms_list, 95)
    delay_max_ms = max(delay_ms_list)

    if jitter_ms_list:
        jitter_avg_ms = mean(jitter_ms_list)
        jitter_p50_ms = percentile(jitter_ms_list, 50)
        jitter_p95_ms = percentile(jitter_ms_list, 95)
        jitter_max_ms = max(jitter_ms_list)
    else:
        jitter_avg_ms = None
        jitter_p50_ms = None
        jitter_p95_ms = None
        jitter_max_ms = None

    print(f"实际测试时长     : {elapsed_s:.3f} s")
    print(f"期望包数         : {expected_count}")
    print(f"估计丢包数       : {loss_count}")
    print(f"估计丢包率       : {loss_rate:.3f}%")
    print(f"无效包率         : {invalid_rate:.3f}%")
    print(f"实际接收频率     : {actual_rate_hz:.3f} Hz")
    print(f"等效接收周期     : {fmt_ms(effective_interval_ms)} ms")

    print("-- 时延统计 (ms) --")
    print(f"最小={delay_min_ms:.3f} 均值={delay_avg_ms:.3f} "
          f"P50={fmt_ms(delay_p50_ms)} "
          f"P95={fmt_ms(delay_p95_ms)} "
          f"最大={delay_max_ms:.3f}")

    if jitter_ms_list:
        print("-- 抖动统计 (ms) --")
        print(f"均值={fmt_ms(jitter_avg_ms)} "
              f"P50={fmt_ms(jitter_p50_ms)} "
              f"P95={fmt_ms(jitter_p95_ms)} "
              f"最大={fmt_ms(jitter_max_ms)}")
    else:
        print("-- 抖动统计 (ms) --")
        print("无（至少需要 2 个有效包）")

    if args.csv:
        csv_path = prepare_csv_output_path(args.csv)
        with csv_path.open("w", newline="", encoding="utf-8-sig") as f:
            writer = csv.writer(f)

            # 区块1：整体统计分析。
            writer.writerow(["统计项", "数值"])
            writer.writerow(["监听IP", args.listen_ip])
            writer.writerow(["端口", args.port])
            writer.writerow(["期望发送周期_ms", f"{args.interval_ms:.6f}"])
            writer.writerow(["配置测试时长_s", f"{args.duration_s:.6f}"])
            writer.writerow(["实际测试时长_s", f"{elapsed_s:.6f}"])
            writer.writerow(["收到原始包数", raw_recv])
            writer.writerow(["收到有效包数", valid_recv])
            writer.writerow(["收到无效包数", invalid_recv])
            writer.writerow(["无效包率_%", f"{invalid_rate:.6f}"])
            writer.writerow(["期望包数", expected_count])
            writer.writerow(["估计丢包数", loss_count])
            writer.writerow(["估计丢包率_%", f"{loss_rate:.6f}"])
            writer.writerow(["实际接收频率_Hz", f"{actual_rate_hz:.6f}"])
            writer.writerow(["等效接收周期_ms", "无" if effective_interval_ms is None else f"{effective_interval_ms:.6f}"])
            writer.writerow(["时延最小值_ms", f"{delay_min_ms:.6f}"])
            writer.writerow(["时延均值_ms", f"{delay_avg_ms:.6f}"])
            writer.writerow(["时延P50_ms", fmt_ms(delay_p50_ms)])
            writer.writerow(["时延P95_ms", fmt_ms(delay_p95_ms)])
            writer.writerow(["时延最大值_ms", f"{delay_max_ms:.6f}"])
            writer.writerow(["抖动均值_ms", fmt_ms(jitter_avg_ms)])
            writer.writerow(["抖动P50_ms", fmt_ms(jitter_p50_ms)])
            writer.writerow(["抖动P95_ms", fmt_ms(jitter_p95_ms)])
            writer.writerow(["抖动最大值_ms", fmt_ms(jitter_max_ms)])
            writer.writerow([])

            # 区块2：逐包明细。
            writer.writerow([
                "接收序号",
                "接收时间戳_s",
                "期望时间戳_s",
                "时延_ms",
                "抖动_ms",
                "源IP",
                "源端口",
            ])
            for r in records:
                writer.writerow([
                    r.recv_index,
                    f"{r.recv_time_s:.9f}",
                    f"{r.expected_time_s:.9f}",
                    f"{r.delay_ms:.6f}",
                    f"{r.jitter_ms:.6f}",
                    r.source_ip,
                    r.source_port,
                ])
                print(f"CSV 已保存: {csv_path}")


if __name__ == "__main__":
    main()
