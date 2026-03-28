#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Logic2 原始数字边沿 CSV（Time [s], Channel X）CAN 自动分析脚本。

能力：
1) 从边沿流自动估计仲裁位时间（可手动覆盖）。
2) 对原始边沿进行毛刺抑制。
3) 自动寻找 SOF 并解码 CAN 帧（优先支持 Classical CAN，兼容部分 CAN-FD 控制字段识别）。
4) 对 RobStride 扩展 ID（29-bit）进行位域拆解与统计。
5) 输出 CAN 性能表、命令统计、参数索引统计、位域统计。
6) 导出分析报告（Markdown）与明细（CSV）。

用法示例：
python test/can/tools/analyze_logic2_digital_can.py test/can/captures/logic2-digital.csv
python test/can/tools/analyze_logic2_digital_can.py test/can/captures/logic2-digital.csv --bitrate 1000000
"""

from __future__ import annotations

import argparse
import bisect
import csv
import math
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, median
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

# RobStride 物理量映射范围（与固件保持一致）
ROBSTRIDE_P_MIN = -12.57
ROBSTRIDE_P_MAX = 12.57
ROBSTRIDE_V_MIN = -50.0
ROBSTRIDE_V_MAX = 50.0
ROBSTRIDE_KP_MIN = 0.0
ROBSTRIDE_KP_MAX = 500.0
ROBSTRIDE_KD_MIN = 0.0
ROBSTRIDE_KD_MAX = 5.0
ROBSTRIDE_T_MIN = -6.0
ROBSTRIDE_T_MAX = 6.0

ROBSTRIDE_CMD_NAMES: Dict[int, str] = {
    0x00: "GET_ID",
    0x01: "MOTION_CTRL",
    0x02: "FEEDBACK",
    0x03: "ENABLE",
    0x04: "STOP",
    0x06: "SET_ZERO",
    0x07: "SET_CAN_ID",
    0x11: "READ_PARAM",
    0x12: "WRITE_PARAM",
    0x15: "FAULT_FEEDBACK",
    0x16: "SAVE_CONFIG",
    0x17: "SET_BAUDRATE",
    0x18: "AUTO_REPORT",
    0x19: "SET_PROTOCOL",
}

ROBSTRIDE_PARAM_NAMES: Dict[int, str] = {
    0x7005: "RUN_MODE",
    0x7006: "IQ_REF",
    0x700A: "SPD_REF",
    0x700B: "LIMIT_TORQUE",
    0x7010: "CUR_KP",
    0x7011: "CUR_KI",
    0x7014: "CUR_FILT_GAIN",
    0x7016: "LOC_REF",
    0x7017: "LIMIT_SPD",
    0x7018: "LIMIT_CUR",
    0x7019: "MECH_POS",
    0x701A: "IQF",
    0x701B: "MECH_VEL",
    0x701C: "VBUS",
    0x701E: "LOC_KP",
    0x701F: "SPD_KP",
    0x7020: "SPD_KI",
    0x7021: "SPD_FILT_GAIN",
    0x7022: "ACC_RAD",
    0x7024: "VEL_MAX",
    0x7025: "ACC_SET",
    0x7026: "EPS_CAN_TIME",
    0x7028: "CAN_TIMEOUT",
    0x7029: "ZERO_STA",
    0x702B: "ADD_OFFSET",
}


@dataclass
class Segment:
    start: float
    end: float
    level: int

    @property
    def duration(self) -> float:
        return self.end - self.start


@dataclass
class DecodedFrame:
    sof_time: float
    end_time: float
    duration_s: float
    can_id: int
    is_extended: bool
    is_fd: bool
    rtr: int
    dlc: int
    payload_len: int
    data: bytes
    cmd_type: Optional[int]
    crc_ok: Optional[bool]
    form_ok: Optional[bool]
    ack_dominant: Optional[bool]
    stuffed_bits: int
    destuffed_bits: int
    sampled_bits: int
    sync_corrections: int


@dataclass
class DecodeAttempt:
    sof_time: float
    success: bool
    reason: str
    sampled_bits: int


@dataclass(frozen=True)
class BitSyncConfig:
    enabled: bool = True
    window: float = 0.35
    sjw: float = 0.20
    min_hold: float = 0.20
    on_any_edge: bool = False


@dataclass(frozen=True)
class BitSamplingConfig:
    vote_count: int = 1
    vote_span: float = 0.0
    frame_sp_retries: int = 0
    frame_sp_step: float = 0.02


def can_crc15(bits: Sequence[int]) -> int:
    """Classical CAN CRC-15（poly=0x4599, init=0）."""
    crc = 0
    poly = 0x4599
    for bit in bits:
        msb = (crc >> 14) & 0x1
        crc = ((crc << 1) & 0x7FFF)
        if (msb ^ (bit & 0x1)) != 0:
            crc ^= poly
    return crc


class SignalSampler:
    def __init__(self, segments: Sequence[Segment]) -> None:
        self.segments = list(segments)
        self.starts = [s.start for s in self.segments]
        self.edge_times = [self.segments[i].start for i in range(1, len(self.segments))]
        self.edge_to_level = [self.segments[i].level for i in range(1, len(self.segments))]
        self.edge_post_durations = [self.segments[i].duration for i in range(1, len(self.segments))]

    def find_index(self, t: float) -> int:
        idx = bisect.bisect_right(self.starts, t) - 1
        if idx < 0:
            return 0
        if idx >= len(self.segments):
            return len(self.segments) - 1
        return idx

    def sample(self, t: float, idx_hint: int) -> Tuple[int, int]:
        idx = idx_hint
        n = len(self.segments)
        while idx < n - 1 and t >= self.segments[idx].end:
            idx += 1
        return self.segments[idx].level, idx

    def level_at(self, t: float) -> int:
        idx = bisect.bisect_right(self.starts, t) - 1
        if idx < 0:
            idx = 0
        elif idx >= len(self.segments):
            idx = len(self.segments) - 1
        return self.segments[idx].level

    def find_edge_index(self, t: float) -> int:
        return bisect.bisect_left(self.edge_times, t)

    def find_sync_correction(
        self,
        expected_boundary: float,
        bit_time: float,
        edge_idx_hint: int,
        cfg: BitSyncConfig,
    ) -> Tuple[float, int]:
        if (not cfg.enabled) or (not self.edge_times):
            return 0.0, edge_idx_hint

        window = max(cfg.window, 0.0) * bit_time
        sjw = max(cfg.sjw, 0.0) * bit_time
        if window <= 0.0 or sjw <= 0.0:
            return 0.0, edge_idx_hint

        hold_need = max(cfg.min_hold, 0.0) * bit_time
        lower = expected_boundary - window
        upper = expected_boundary + window

        n = len(self.edge_times)
        edge_idx = max(0, min(edge_idx_hint, n))

        while edge_idx > 0 and self.edge_times[edge_idx - 1] >= lower:
            edge_idx -= 1
        while edge_idx < n and self.edge_times[edge_idx] < lower:
            edge_idx += 1

        best_idx = -1
        best_err = 0.0
        j = edge_idx
        while j < n and self.edge_times[j] <= upper:
            if self.edge_post_durations[j] >= hold_need:
                if cfg.on_any_edge or self.edge_to_level[j] == 0:
                    err = self.edge_times[j] - expected_boundary
                    if best_idx < 0 or abs(err) < abs(best_err):
                        best_idx = j
                        best_err = err
            j += 1

        if best_idx < 0:
            return 0.0, edge_idx

        correction = max(-sjw, min(sjw, best_err))
        return correction, min(best_idx + 1, n)


def bits_to_int(bits: Sequence[int]) -> int:
    v = 0
    for b in bits:
        v = (v << 1) | (b & 1)
    return v


def uint16_to_float(x: int, x_min: float, x_max: float) -> float:
    span = x_max - x_min
    return (x / 65535.0) * span + x_min


def dlc_to_fd_length(dlc: int) -> int:
    if dlc <= 8:
        return dlc
    fd_map = {
        9: 12,
        10: 16,
        11: 20,
        12: 24,
        13: 32,
        14: 48,
        15: 64,
    }
    return fd_map.get(dlc, 0)


def percentile(values: Sequence[float], p: float) -> Optional[float]:
    if not values:
        return None
    vals = sorted(values)
    if p <= 0:
        return vals[0]
    if p >= 100:
        return vals[-1]
    rank = (len(vals) - 1) * (p / 100.0)
    lo = int(math.floor(rank))
    hi = int(math.ceil(rank))
    if lo == hi:
        return vals[lo]
    frac = rank - lo
    return vals[lo] * (1.0 - frac) + vals[hi] * frac


def fmt_float(v: Optional[float], digits: int = 3) -> str:
    if v is None:
        return "-"
    return f"{v:.{digits}f}"


def read_logic2_edge_csv(csv_path: Path) -> Tuple[List[float], List[int]]:
    times: List[float] = []
    levels: List[int] = []

    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header is None:
            raise ValueError("CSV 为空")

        prev_level: Optional[int] = None
        for row in reader:
            if len(row) < 2:
                continue

            try:
                t = float(row[0].strip())
                lv = int(float(row[1].strip()))
            except ValueError:
                continue

            lv = 1 if lv != 0 else 0

            if not times:
                times.append(t)
                levels.append(lv)
                prev_level = lv
                continue

            # 同时刻重复样本：保留最后电平
            if t == times[-1]:
                levels[-1] = lv
                prev_level = lv
                continue

            # 非边沿（电平不变）忽略
            if prev_level is not None and lv == prev_level:
                continue

            times.append(t)
            levels.append(lv)
            prev_level = lv

    if len(times) < 2:
        raise ValueError("有效边沿数量不足，无法分析")

    return times, levels


def build_segments(times: Sequence[float], levels: Sequence[int]) -> List[Segment]:
    deltas = [times[i + 1] - times[i] for i in range(len(times) - 1) if times[i + 1] > times[i]]
    tail = median(deltas) if deltas else 1e-6
    tail = max(tail * 5.0, 5e-6)

    segments: List[Segment] = []
    for i in range(len(times) - 1):
        if times[i + 1] <= times[i]:
            continue
        segments.append(Segment(times[i], times[i + 1], levels[i]))

    segments.append(Segment(times[-1], times[-1] + tail, levels[-1]))
    return segments


def filter_glitches(segments: List[Segment], glitch_threshold_s: float) -> List[Segment]:
    segs = [Segment(s.start, s.end, s.level) for s in segments]

    i = 1
    while i < len(segs) - 1:
        curr = segs[i]
        prev = segs[i - 1]
        nxt = segs[i + 1]

        if curr.duration < glitch_threshold_s and prev.level == nxt.level:
            prev.end = nxt.end
            del segs[i : i + 2]
            if i > 1:
                i -= 1
            continue

        i += 1

    return segs


def estimate_bit_time(segments: Sequence[Segment]) -> float:
    dts = [s.duration for s in segments if 0 < s.duration < 20e-6]
    if not dts:
        return 1e-6

    known_bt = [8e-6, 4e-6, 2e-6, 1e-6, 0.5e-6, 0.25e-6]
    best_bt = 1e-6
    best_score = -1

    for bt in known_bt:
        tol = 0.15 * bt
        score = 0
        for d in dts:
            n = round(d / bt)
            if 1 <= n <= 24 and abs(d - n * bt) <= tol:
                score += 1
        if score > best_score:
            best_score = score
            best_bt = bt

    return best_bt


def detect_sof_candidates(
    segments: Sequence[Segment],
    bit_time: float,
    sample_point: float,
    idle_bits: int = 11,
    min_dominant_hold: float = 0.55,
    min_edges_in_window: int = 2,
    edge_window_bits: int = 20,
) -> List[float]:
    sof_times: List[float] = []
    idle_need = idle_bits * bit_time
    starts = [s.start for s in segments]
    hold_need = max(min_dominant_hold * bit_time, 0.2 * bit_time)

    for i in range(1, len(segments)):
        prev = segments[i - 1]
        curr = segments[i]
        if not (prev.level == 1 and curr.level == 0 and prev.duration >= idle_need):
            continue

        # 候选过滤：SOF 采样点必须仍处于 dominant 段，且首段不能过短
        if curr.duration < hold_need:
            continue
        if curr.start + sample_point * bit_time >= curr.end:
            continue

        # 候选过滤：在首个窗口内应出现一定边沿密度，抑制孤立毛刺触发
        if min_edges_in_window > 0 and edge_window_bits > 0:
            window_end = curr.start + edge_window_bits * bit_time
            j = bisect.bisect_left(starts, window_end, lo=i + 1)
            edge_cnt = max(0, j - (i + 1))
            if edge_cnt < min_edges_in_window:
                continue

        sof_times.append(curr.start)

    return sof_times


def decode_one_frame(
    sampler: SignalSampler,
    sof_time: float,
    bit_time: float,
    sample_point: float = 0.8,
    max_bits: int = 800,
    sync_cfg: Optional[BitSyncConfig] = None,
    sample_vote_offsets: Optional[Sequence[float]] = None,
) -> Tuple[Optional[DecodedFrame], str, int]:
    if sync_cfg is None:
        sync_cfg = BitSyncConfig()
    if not sample_vote_offsets:
        sample_vote_offsets = (0.0,)
    vote_offsets = tuple(sample_vote_offsets)

    bit_start = sof_time
    t = bit_start + sample_point * bit_time
    idx = sampler.find_index(t)
    edge_idx = sampler.find_edge_index(bit_start)

    destuffed: List[int] = []
    post_bits: List[int] = []

    stuff_enabled = True
    prev: Optional[int] = None
    run_len = 0

    is_extended: Optional[bool] = None
    is_fd = False
    expected_crc_end: Optional[int] = None
    data_start: Optional[int] = None
    payload_len = 0
    dlc = 0
    rtr = 0
    stuffed_bits = 0
    sync_corrections = 0

    samples = 0

    def fail(reason: str) -> Tuple[None, str, int]:
        return None, reason, samples

    for _ in range(max_bits):
        center_t = t
        center_bit, idx = sampler.sample(center_t, idx)
        if len(vote_offsets) <= 1:
            bit = center_bit
        else:
            ones = 0
            for off in vote_offsets:
                sp_local = sample_point + off
                if sp_local < 0.02:
                    sp_local = 0.02
                elif sp_local > 0.98:
                    sp_local = 0.98
                sample_t = bit_start + sp_local * bit_time
                ones += sampler.level_at(sample_t)
            bit = 1 if ones > (len(vote_offsets) // 2) else 0
        samples += 1

        consumed_as_stuff = False

        if stuff_enabled:
            if prev is not None and run_len == 5:
                # Stuff bit（应为反相）
                stuffed_bits += 1
                if bit == prev:
                    return fail("stuff_error")
                prev = bit
                run_len = 1
                consumed_as_stuff = True

            if not consumed_as_stuff:
                destuffed.append(bit)

                if prev is None:
                    prev = bit
                    run_len = 1
                elif bit == prev:
                    run_len += 1
                else:
                    prev = bit
                    run_len = 1

                # 最基础合法性：SOF 必须为 dominant(0)
                if len(destuffed) == 1 and destuffed[0] != 0:
                    return fail("invalid_sof")

                if len(destuffed) >= 14 and is_extended is None:
                    ide = destuffed[13]
                    is_extended = (ide == 1)

                if is_extended is not None and expected_crc_end is None:
                    if not is_extended:
                        if len(destuffed) >= 19:
                            rtr = destuffed[12]
                            dlc = bits_to_int(destuffed[15:19])
                            payload_len = 0 if rtr else min(dlc, 8)
                            data_start = 19
                            expected_crc_end = data_start + payload_len * 8 + 15
                    else:
                        # 先等到控制字段完整，再判定 Classic/FD
                        if len(destuffed) >= 39:
                            # 扩展 Classic: bit34=r0, DLC在35~38
                            # 扩展 FD: bit34=FDF(=1), DLC在38~41
                            if len(destuffed) >= 42 and destuffed[34] == 1:
                                is_fd = True
                                dlc = bits_to_int(destuffed[38:42])
                                payload_len = dlc_to_fd_length(dlc)
                                crc_len = 17 if payload_len <= 16 else 21
                                data_start = 42
                                expected_crc_end = data_start + payload_len * 8 + crc_len
                                rtr = destuffed[32]
                            elif destuffed[34] == 0:
                                is_fd = False
                                dlc = bits_to_int(destuffed[35:39])
                                rtr = destuffed[32]
                                payload_len = 0 if rtr else min(dlc, 8)
                                data_start = 39
                                expected_crc_end = data_start + payload_len * 8 + 15

                if expected_crc_end is not None and len(destuffed) >= expected_crc_end:
                    stuff_enabled = False
                    post_bits = []

        else:
            post_bits.append(bit)
            # CRC delimiter + ACK + ACK delimiter + EOF(7)
            if len(post_bits) >= 10:
                break

        next_boundary = bit_start + bit_time
        if sync_cfg.enabled:
            corr, edge_idx = sampler.find_sync_correction(next_boundary, bit_time, edge_idx, sync_cfg)
            if abs(corr) > 1e-15:
                sync_corrections += 1
            bit_start = next_boundary + corr
        else:
            bit_start = next_boundary

        t = bit_start + sample_point * bit_time

    if is_extended is None or expected_crc_end is None or data_start is None:
        return fail("header_incomplete")

    if len(destuffed) < expected_crc_end:
        return fail("crc_or_data_incomplete")

    if len(post_bits) < 10:
        return fail("trailer_incomplete")

    # 解析 ID
    if is_extended:
        id_a = bits_to_int(destuffed[1:12])
        id_b = bits_to_int(destuffed[14:32])
        can_id = (id_a << 18) | id_b
    else:
        can_id = bits_to_int(destuffed[1:12])

    # 解析数据
    data_bits = destuffed[data_start : data_start + payload_len * 8]
    if len(data_bits) != payload_len * 8:
        return fail("payload_length_mismatch")

    data_bytes = bytearray()
    for i in range(payload_len):
        data_bytes.append(bits_to_int(data_bits[i * 8 : (i + 1) * 8]))

    crc_ok: Optional[bool] = None
    if not is_fd:
        crc_start = data_start + payload_len * 8
        crc_bits = destuffed[crc_start:expected_crc_end]
        if len(crc_bits) != 15:
            return fail("crc_length_invalid")
        crc_recv = bits_to_int(crc_bits)
        crc_calc = can_crc15(destuffed[:crc_start])
        crc_ok = (crc_recv == crc_calc)

    crc_delim_ok = (post_bits[0] == 1)
    ack_slot = post_bits[1]
    ack_dominant = (ack_slot == 0)
    ack_delim_ok = (post_bits[2] == 1)
    eof_ok = all(b == 1 for b in post_bits[3:10])
    form_ok = crc_delim_ok and ack_delim_ok and eof_ok

    cmd_type = ((can_id >> 24) & 0x1F) if is_extended else None
    end_time = max(bit_start, sof_time)

    frame = DecodedFrame(
        sof_time=sof_time,
        end_time=end_time,
        duration_s=max(end_time - sof_time, 0.0),
        can_id=can_id,
        is_extended=is_extended,
        is_fd=is_fd,
        rtr=rtr,
        dlc=dlc,
        payload_len=payload_len,
        data=bytes(data_bytes),
        cmd_type=cmd_type,
        crc_ok=crc_ok,
        form_ok=form_ok,
        ack_dominant=ack_dominant,
        stuffed_bits=stuffed_bits,
        destuffed_bits=len(destuffed),
        sampled_bits=samples,
        sync_corrections=sync_corrections,
    )
    return frame, "ok", samples


def decode_frames(
    segments: Sequence[Segment],
    bit_time: float,
    sample_point: float,
    max_candidates: Optional[int] = None,
    sof_idle_bits: int = 11,
    sof_min_hold: float = 0.55,
    sof_min_edges: int = 2,
    sof_edge_window_bits: int = 20,
    sync_cfg: Optional[BitSyncConfig] = None,
    sample_vote_offsets: Optional[Sequence[float]] = None,
    frame_sp_offsets: Optional[Sequence[float]] = None,
) -> Tuple[List[DecodedFrame], List[DecodeAttempt], int]:
    if sync_cfg is None:
        sync_cfg = BitSyncConfig()
    if not sample_vote_offsets:
        sample_vote_offsets = (0.0,)
    if not frame_sp_offsets:
        frame_sp_offsets = (0.0,)

    def frame_quality_score(frame: DecodedFrame) -> float:
        score = 0.0
        if frame.crc_ok is True:
            score += 4.0
        elif frame.crc_ok is False:
            score -= 2.0

        if frame.form_ok is True:
            score += 2.0
        elif frame.form_ok is False:
            score -= 1.0

        if frame.ack_dominant is True:
            score += 1.0
        elif frame.ack_dominant is False:
            score -= 0.5

        score += min(frame.payload_len, 8) * 0.02
        return score

    sampler = SignalSampler(segments)
    sof_candidates = detect_sof_candidates(
        segments,
        bit_time,
        sample_point=sample_point,
        idle_bits=sof_idle_bits,
        min_dominant_hold=sof_min_hold,
        min_edges_in_window=sof_min_edges,
        edge_window_bits=sof_edge_window_bits,
    )
    if max_candidates is not None and len(sof_candidates) > max_candidates:
        # 候选过多时做全窗口均匀抽样，避免只截取前段导致参数选择偏差
        stride = len(sof_candidates) / max_candidates
        sampled: List[float] = []
        for i in range(max_candidates):
            idx = min(int(i * stride), len(sof_candidates) - 1)
            sampled.append(sof_candidates[idx])
        sof_candidates = sampled

    frames: List[DecodedFrame] = []
    attempts: List[DecodeAttempt] = []
    last_end = -1.0

    for sof in sof_candidates:
        if sof <= last_end + 2.0 * bit_time:
            continue

        best_frame: Optional[DecodedFrame] = None
        best_score = -1e12
        best_reason = "decode_failed"
        best_sampled_bits = 0
        tested_sp: set[int] = set()

        for sp_off in frame_sp_offsets:
            sp_try = min(max(sample_point + sp_off, 0.05), 0.95)
            sp_key = int(round(sp_try * 10000))
            if sp_key in tested_sp:
                continue
            tested_sp.add(sp_key)

            frame, reason, sampled_bits = decode_one_frame(
                sampler,
                sof,
                bit_time,
                sample_point=sp_try,
                sync_cfg=sync_cfg,
                sample_vote_offsets=sample_vote_offsets,
            )

            if frame is None:
                if best_frame is None and sampled_bits > best_sampled_bits:
                    best_reason = reason
                    best_sampled_bits = sampled_bits
                continue

            score = frame_quality_score(frame)
            if best_frame is None or score > best_score:
                best_frame = frame
                best_score = score
                best_sampled_bits = sampled_bits

            # 达到较高帧质量后提前结束该候选，避免重复尝试。
            if score >= 6.0:
                break

        if best_frame is None:
            attempts.append(
                DecodeAttempt(
                    sof_time=sof,
                    success=False,
                    reason=best_reason,
                    sampled_bits=best_sampled_bits,
                )
            )
            continue

        frames.append(best_frame)
        attempts.append(
            DecodeAttempt(
                sof_time=sof,
                success=True,
                reason="ok",
                sampled_bits=best_sampled_bits,
            )
        )
        last_end = best_frame.end_time

    return frames, attempts, len(sof_candidates)


def evaluate_decode_quality(frames: Sequence[DecodedFrame], attempts: Sequence[DecodeAttempt]) -> float:
    if not frames:
        return 0.0

    decoded = len(frames)
    attempt_total = len(attempts)
    success_ratio = decoded / max(1, attempt_total)

    crc_checked = sum(1 for f in frames if f.crc_ok is not None)
    crc_fail = sum(1 for f in frames if f.crc_ok is False)
    form_fail = sum(1 for f in frames if f.form_ok is False)
    ack_missing = sum(1 for f in frames if f.ack_dominant is False)
    clean = sum(
        1
        for f in frames
        if (f.crc_ok is not False) and (f.form_ok is not False) and (f.ack_dominant is not False)
    )

    crc_pass_ratio = (crc_checked - crc_fail) / max(1, crc_checked)
    form_pass_ratio = (decoded - form_fail) / max(1, decoded)
    ack_pass_ratio = (decoded - ack_missing) / max(1, decoded)
    clean_ratio = clean / max(1, decoded)

    # 评分兼顾：解码数量、尝试成功率、帧质量。
    # 不采用全乘积，避免在 CRC 全失败时所有候选评分都归零。
    decode_bonus = decoded * (0.2 + 0.8 * success_ratio)
    quality_bonus = clean * (1.0 + clean_ratio + 0.5 * crc_pass_ratio + 0.3 * form_pass_ratio + 0.2 * ack_pass_ratio)
    penalty = 0.05 * crc_fail + 0.03 * form_fail + 0.02 * ack_missing

    return max(0.0, decode_bonus + quality_bonus - penalty)


def build_sample_point_candidates(sample_point_seed: float) -> List[float]:
    raw = [
        sample_point_seed,
        sample_point_seed - 0.03,
        sample_point_seed + 0.03,
        sample_point_seed - 0.05,
        sample_point_seed + 0.05,
        sample_point_seed - 0.08,
        sample_point_seed + 0.08,
        0.70,
        0.75,
        0.80,
        0.85,
    ]

    dedup: List[float] = []
    for sp in raw:
        if sp <= 0.55 or sp >= 0.95:
            continue
        if not any(abs(sp - x) < 1e-4 for x in dedup):
            dedup.append(sp)
    return dedup


def build_vote_offsets(vote_count: int, vote_span: float) -> List[float]:
    if vote_count <= 1 or vote_span <= 0:
        return [0.0]

    half = vote_count // 2
    if half <= 0:
        return [0.0]

    step = vote_span / half
    return [i * step for i in range(-half, half + 1)]


def build_frame_sp_offsets(frame_sp_retries: int, frame_sp_step: float) -> List[float]:
    offsets = [0.0]
    if frame_sp_retries <= 0 or frame_sp_step <= 0:
        return offsets

    for i in range(1, frame_sp_retries + 1):
        delta = i * frame_sp_step
        offsets.append(delta)
        offsets.append(-delta)
    return offsets


def choose_best_timing(
    segments: Sequence[Segment],
    auto_bit_time: float,
    sample_point_seed: float,
    sof_idle_bits: int,
    sof_min_hold: float,
    sof_min_edges: int,
    sof_edge_window_bits: int,
    sync_cfg: Optional[BitSyncConfig],
    sample_vote_offsets: Sequence[float],
    frame_sp_offsets: Sequence[float],
) -> Tuple[float, float, List[DecodedFrame], List[DecodeAttempt], int]:
    candidates_bt = [auto_bit_time, auto_bit_time * 0.97, auto_bit_time * 1.03, 1e-6, 2e-6, 0.5e-6]
    sample_candidates = build_sample_point_candidates(sample_point_seed)

    dedup: List[float] = []
    for bt in candidates_bt:
        if bt <= 0:
            continue
        if not any(abs(bt - x) / x < 1e-3 for x in dedup):
            dedup.append(bt)

    best_bt = dedup[0]
    best_sp = sample_candidates[0]
    best_frames: List[DecodedFrame] = []
    best_attempts: List[DecodeAttempt] = []
    best_score = -1.0

    # 先用前 1500 个候选 SOF 粗选，避免全量多次重跑
    for bt in dedup:
        for sp in sample_candidates:
            frames, attempts, _ = decode_frames(
                segments,
                bt,
                sample_point=sp,
                max_candidates=1500,
                sof_idle_bits=sof_idle_bits,
                sof_min_hold=sof_min_hold,
                sof_min_edges=sof_min_edges,
                sof_edge_window_bits=sof_edge_window_bits,
                sync_cfg=sync_cfg,
                sample_vote_offsets=sample_vote_offsets,
                frame_sp_offsets=frame_sp_offsets,
            )
            score = evaluate_decode_quality(frames, attempts)

            if score > best_score:
                best_bt = bt
                best_sp = sp
                best_frames = frames
                best_attempts = attempts
                best_score = score

    # 使用最佳 bt 全量解码
    if best_score <= 0 and best_frames:
        best_score = evaluate_decode_quality(best_frames, best_attempts)

    full_frames, attempts, sof_total = decode_frames(
        segments,
        best_bt,
        sample_point=best_sp,
        max_candidates=None,
        sof_idle_bits=sof_idle_bits,
        sof_min_hold=sof_min_hold,
        sof_min_edges=sof_min_edges,
        sof_edge_window_bits=sof_edge_window_bits,
        sync_cfg=sync_cfg,
        sample_vote_offsets=sample_vote_offsets,
        frame_sp_offsets=frame_sp_offsets,
    )
    return best_bt, best_sp, full_frames, attempts, sof_total


def choose_best_sample_point(
    segments: Sequence[Segment],
    bit_time: float,
    sample_point_seed: float,
    sof_idle_bits: int,
    sof_min_hold: float,
    sof_min_edges: int,
    sof_edge_window_bits: int,
    sync_cfg: Optional[BitSyncConfig],
    sample_vote_offsets: Sequence[float],
    frame_sp_offsets: Sequence[float],
) -> Tuple[float, List[DecodedFrame], List[DecodeAttempt], int]:
    sample_candidates = build_sample_point_candidates(sample_point_seed)

    best_sp = sample_candidates[0]
    best_score = -1.0
    best_frames: List[DecodedFrame] = []
    best_attempts: List[DecodeAttempt] = []

    for sp in sample_candidates:
        frames, attempts, _ = decode_frames(
            segments,
            bit_time,
            sample_point=sp,
            max_candidates=1500,
            sof_idle_bits=sof_idle_bits,
            sof_min_hold=sof_min_hold,
            sof_min_edges=sof_min_edges,
            sof_edge_window_bits=sof_edge_window_bits,
            sync_cfg=sync_cfg,
            sample_vote_offsets=sample_vote_offsets,
            frame_sp_offsets=frame_sp_offsets,
        )
        score = evaluate_decode_quality(frames, attempts)
        if score > best_score:
            best_sp = sp
            best_frames = frames
            best_attempts = attempts
            best_score = score

    if best_score <= 0 and best_frames:
        best_score = evaluate_decode_quality(best_frames, best_attempts)

    full_frames, attempts, sof_total = decode_frames(
        segments,
        bit_time,
        sample_point=best_sp,
        max_candidates=None,
        sof_idle_bits=sof_idle_bits,
        sof_min_hold=sof_min_hold,
        sof_min_edges=sof_min_edges,
        sof_edge_window_bits=sof_edge_window_bits,
        sync_cfg=sync_cfg,
        sample_vote_offsets=sample_vote_offsets,
        frame_sp_offsets=frame_sp_offsets,
    )
    return best_sp, full_frames, attempts, sof_total


def cmd_name(cmd: Optional[int]) -> str:
    if cmd is None:
        return "-"
    return ROBSTRIDE_CMD_NAMES.get(cmd, f"UNKNOWN_0x{cmd:02X}")


def parse_frame_fields(frame: DecodedFrame) -> Dict[str, object]:
    out: Dict[str, object] = {}

    out["timestamp_s"] = frame.sof_time
    out["id_hex"] = f"0x{frame.can_id:08X}" if frame.is_extended else f"0x{frame.can_id:03X}"
    out["is_extended"] = int(frame.is_extended)
    out["is_fd"] = int(frame.is_fd)
    out["dlc"] = frame.dlc
    out["payload_len"] = frame.payload_len
    out["data_hex"] = " ".join(f"{b:02X}" for b in frame.data)
    out["crc_ok"] = "-" if frame.crc_ok is None else int(frame.crc_ok)
    out["form_ok"] = "-" if frame.form_ok is None else int(frame.form_ok)
    out["ack_dominant"] = "-" if frame.ack_dominant is None else int(frame.ack_dominant)
    out["sampled_bits"] = frame.sampled_bits
    out["stuffed_bits"] = frame.stuffed_bits
    out["destuffed_bits"] = frame.destuffed_bits
    out["sync_corrections"] = frame.sync_corrections

    if frame.is_extended:
        cmd = frame.cmd_type
        out["cmd_type"] = f"0x{cmd:02X}" if cmd is not None else "-"
        out["cmd_name"] = cmd_name(cmd)
        out["id_field_23_16"] = (frame.can_id >> 16) & 0xFF
        out["id_field_15_8"] = (frame.can_id >> 8) & 0xFF
        out["id_field_7_0"] = frame.can_id & 0xFF

        cmd_val = cmd if cmd is not None else -1
        data = frame.data

        if cmd_val in (0x02, 0x18) and len(data) >= 8:
            pos_raw = (data[0] << 8) | data[1]
            vel_raw = (data[2] << 8) | data[3]
            tor_raw = (data[4] << 8) | data[5]
            tmp_raw = (data[6] << 8) | data[7]

            out["feedback_mode"] = (frame.can_id >> 22) & 0x03
            out["feedback_fault_flags"] = (frame.can_id >> 16) & 0x3F
            out["feedback_motor_id"] = (frame.can_id >> 8) & 0xFF
            out["feedback_host_id"] = frame.can_id & 0xFF
            out["feedback_pos_rad"] = uint16_to_float(pos_raw, ROBSTRIDE_P_MIN, ROBSTRIDE_P_MAX)
            out["feedback_vel_rad_s"] = uint16_to_float(vel_raw, ROBSTRIDE_V_MIN, ROBSTRIDE_V_MAX)
            out["feedback_torque_nm"] = uint16_to_float(tor_raw, ROBSTRIDE_T_MIN, ROBSTRIDE_T_MAX)
            out["feedback_temp_c"] = tmp_raw / 10.0

        elif cmd_val == 0x01 and len(data) >= 8:
            pos_raw = (data[0] << 8) | data[1]
            vel_raw = (data[2] << 8) | data[3]
            kp_raw = (data[4] << 8) | data[5]
            kd_raw = (data[6] << 8) | data[7]
            tq_raw = (frame.can_id >> 8) & 0xFFFF

            out["target_motor_id"] = frame.can_id & 0xFF
            out["torque_ff_nm"] = uint16_to_float(tq_raw, ROBSTRIDE_T_MIN, ROBSTRIDE_T_MAX)
            out["target_pos_rad"] = uint16_to_float(pos_raw, ROBSTRIDE_P_MIN, ROBSTRIDE_P_MAX)
            out["target_vel_rad_s"] = uint16_to_float(vel_raw, ROBSTRIDE_V_MIN, ROBSTRIDE_V_MAX)
            out["target_kp"] = uint16_to_float(kp_raw, ROBSTRIDE_KP_MIN, ROBSTRIDE_KP_MAX)
            out["target_kd"] = uint16_to_float(kd_raw, ROBSTRIDE_KD_MIN, ROBSTRIDE_KD_MAX)

        elif cmd_val == 0x11 and len(data) >= 2:
            idx = data[0] | (data[1] << 8)
            out["param_index"] = f"0x{idx:04X}"
            out["param_name"] = ROBSTRIDE_PARAM_NAMES.get(idx, "UNKNOWN")
            out["target_motor_id"] = frame.can_id & 0xFF

        elif cmd_val == 0x12 and len(data) >= 8:
            idx = data[0] | (data[1] << 8)
            out["param_index"] = f"0x{idx:04X}"
            out["param_name"] = ROBSTRIDE_PARAM_NAMES.get(idx, "UNKNOWN")
            out["target_motor_id"] = frame.can_id & 0xFF
            out["param_u8"] = data[4]
            out["param_f32"] = struct.unpack("<f", data[4:8])[0]

    return out


def peak_rate_hz(timestamps: Sequence[float], window_s: float) -> float:
    if not timestamps or window_s <= 0:
        return 0.0
    ts = sorted(timestamps)
    i = 0
    best = 0
    for j, t in enumerate(ts):
        while i <= j and (t - ts[i]) > window_s:
            i += 1
        n = j - i + 1
        if n > best:
            best = n
    return best / window_s


def mean_or_default(values: Sequence[float], default: float) -> float:
    return mean(values) if values else default


def build_markdown_table(headers: Sequence[str], rows: Sequence[Sequence[object]]) -> str:
    lines: List[str] = []
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join(["---"] * len(headers)) + " |")
    for row in rows:
        cells = [str(c) for c in row]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def summarize(
    frames: Sequence[DecodedFrame],
    attempts: Sequence[DecodeAttempt],
    sof_total: int,
    capture_start: float,
    capture_end: float,
    bit_time: float,
    sample_point: float,
    proj_motors: int,
    proj_sensor_hz: float,
    proj_motor_hz_override: float,
    proj_overhead: float,
    proj_ref_feedback_hz: float,
) -> Dict[str, List[List[object]]]:
    report: Dict[str, List[List[object]]] = {}

    duration_capture = max(capture_end - capture_start, 1e-9)
    bitrate = 1.0 / bit_time if bit_time > 0 else 0.0

    attempt_total = len(attempts)
    attempt_success = sum(1 for a in attempts if a.success)
    attempt_fail = attempt_total - attempt_success
    overlap_skipped = max(sof_total - attempt_total, 0)
    reason_counter = Counter(a.reason for a in attempts if not a.success)
    sampled_bits_total = sum(a.sampled_bits for a in attempts if a.sampled_bits > 0)

    if not frames:
        report["perf"] = [
            ["捕获窗口(s)", f"{duration_capture:.6f}"],
            ["仲裁位时间(估计,us)", f"{bit_time * 1e6:.3f}"],
            ["仲裁位率(估计,kbps)", f"{(bitrate / 1000.0):.3f}"],
            ["采样点", f"{sample_point:.3f}"],
            ["解码总帧数", 0],
            ["结论", "未成功解码 CAN 帧，请调整位率/采样点参数"],
        ]
        report["quality"] = [
            ["SOF候选数", sof_total],
            ["实际解码尝试数", attempt_total],
            ["尝试成功数", attempt_success],
            ["尝试失败数", attempt_fail],
            ["尝试成功率(%)", f"{(100.0 * attempt_success / attempt_total) if attempt_total else 0.0:.3f}"],
            ["重叠候选跳过数", overlap_skipped],
            ["采样总位数", sampled_bits_total],
            ["候选噪声率(%)", f"{(100.0 * attempt_fail / attempt_total) if attempt_total else 0.0:.6f}"],
        ]
        report["error_model"] = [
            ["候选层(含误触发)", "-", "-", "无成功解码帧"],
            ["帧层(仅已解码帧)", "-", "-", "无成功解码帧"],
        ]
        report["errors"] = [[k, v] for k, v in reason_counter.most_common()]
        report["id"] = []
        report["cmd"] = []
        report["param"] = []
        report["bitfield"] = []
        report["projection"] = []
        report["projection_assumptions"] = []
        return report

    frames_sorted = sorted(frames, key=lambda f: f.sof_time)
    duration_frame_span = max(frames_sorted[-1].end_time - frames_sorted[0].sof_time, 1e-9)

    total = len(frames_sorted)
    ext_cnt = sum(1 for f in frames_sorted if f.is_extended)
    fd_cnt = sum(1 for f in frames_sorted if f.is_fd)
    sum_frame_time = sum(f.duration_s for f in frames_sorted)
    bus_load = 100.0 * sum_frame_time / duration_capture
    avg_fps = total / duration_capture
    frame_bits = [f.sampled_bits for f in frames_sorted if f.sampled_bits > 0]
    frame_bits_p50 = percentile(frame_bits, 50)
    frame_bits_p95 = percentile(frame_bits, 95)
    peak_fps_100ms = peak_rate_hz([f.sof_time for f in frames_sorted], 0.1)
    peak_fps_1s = peak_rate_hz([f.sof_time for f in frames_sorted], 1.0)

    report["perf"] = [
        ["捕获窗口(s)", f"{duration_capture:.6f}"],
        ["仲裁位时间(估计,us)", f"{bit_time * 1e6:.3f}"],
        ["仲裁位率(估计,kbps)", f"{(bitrate / 1000.0):.3f}"],
        ["采样点", f"{sample_point:.3f}"],
        ["解码总帧数", total],
        ["扩展帧数量", ext_cnt],
        ["CAN-FD帧数量", fd_cnt],
        ["平均帧率(fps)", f"{avg_fps:.3f}"],
        ["峰值帧率(100ms窗,fps)", f"{peak_fps_100ms:.3f}"],
        ["峰值帧率(1s窗,fps)", f"{peak_fps_1s:.3f}"],
        ["帧长度P50(bit)", fmt_float(frame_bits_p50, 2)],
        ["帧长度P95(bit)", fmt_float(frame_bits_p95, 2)],
        ["总线占用率(按帧时长估算,%)", f"{bus_load:.3f}"],
        ["解码帧时间跨度(s)", f"{duration_frame_span:.6f}"],
    ]

    crc_checked = sum(1 for f in frames_sorted if f.crc_ok is not None)
    crc_fail = sum(1 for f in frames_sorted if f.crc_ok is False)
    form_fail = sum(1 for f in frames_sorted if f.form_ok is False)
    ack_missing = sum(1 for f in frames_sorted if f.ack_dominant is False)
    stuff_error = reason_counter.get("stuff_error", 0)
    clean_frames = sum(
        1
        for f in frames_sorted
        if (f.crc_ok is not False) and (f.form_ok is not False) and (f.ack_dominant is not False)
    )
    sync_total = sum(f.sync_corrections for f in frames_sorted)
    sync_avg = sync_total / max(total, 1)

    # 下界估计：每类事件至少对应1位错误（或帧级错误）
    ber_lower_bound = None
    if sampled_bits_total > 0:
        lower_events = crc_fail + form_fail + ack_missing + stuff_error
        ber_lower_bound = lower_events / sampled_bits_total

    frame_sampled_bits_total = sum(f.sampled_bits for f in frames_sorted if f.sampled_bits > 0)
    frame_ber_lower_bound = None
    if frame_sampled_bits_total > 0:
        frame_lower_events = crc_fail + form_fail + ack_missing
        frame_ber_lower_bound = frame_lower_events / frame_sampled_bits_total

    candidate_noise_rate = (attempt_fail / attempt_total) if attempt_total else 0.0
    frame_net_error_rate = 1.0 - (clean_frames / max(total, 1))

    frame_error_proxy = None
    if attempt_total > 0:
        frame_bad = total - clean_frames
        frame_error_proxy = (attempt_fail + frame_bad) / attempt_total

    report["quality"] = [
        ["SOF候选数", sof_total],
        ["实际解码尝试数", attempt_total],
        ["尝试成功数", attempt_success],
        ["尝试失败数", attempt_fail],
        ["尝试成功率(%)", f"{(100.0 * attempt_success / attempt_total) if attempt_total else 0.0:.3f}"],
        ["重叠候选跳过数", overlap_skipped],
        ["CRC校验帧数", crc_checked],
        ["CRC失败帧数", crc_fail],
        ["CRC错误率(%)", f"{(100.0 * crc_fail / crc_checked) if crc_checked else 0.0:.6f}"],
        ["格式错误帧数", form_fail],
        ["ACK缺失帧数", ack_missing],
        ["校验通过帧数", clean_frames],
        ["校验通过率(%)", f"{(100.0 * clean_frames / total) if total else 0.0:.6f}"],
        ["Stuff错误次数", stuff_error],
        ["位同步修正次数", sync_total],
        ["平均每帧位同步修正", f"{sync_avg:.3f}"],
        ["采样总位数", sampled_bits_total],
        ["候选噪声率(%)", f"{100.0 * candidate_noise_rate:.6f}"],
        ["帧级净错误率(%)", f"{100.0 * frame_net_error_rate:.6f}"],
        ["候选层误码率下界(BER_lb)", fmt_float(ber_lower_bound, 9)],
        ["候选层帧错误率代理(FER_proxy)", fmt_float(frame_error_proxy, 9)],
        ["帧级净误码率下界(BER_lb_frame)", fmt_float(frame_ber_lower_bound, 9)],
    ]

    report["error_model"] = [
        [
            "候选层(含误触发)",
            fmt_float(ber_lower_bound, 9),
            fmt_float(frame_error_proxy, 9),
            f"attempt={attempt_total}, fail={attempt_fail}",
        ],
        [
            "帧层(仅已解码帧)",
            fmt_float(frame_ber_lower_bound, 9),
            fmt_float(frame_net_error_rate, 9),
            f"clean={clean_frames}/{total}",
        ],
    ]

    report["errors"] = [[k, v] for k, v in reason_counter.most_common()]

    # ID 统计
    id_groups: Dict[Tuple[int, bool], List[DecodedFrame]] = defaultdict(list)
    for f in frames_sorted:
        id_groups[(f.can_id, f.is_extended)].append(f)

    id_rows: List[List[object]] = []
    for (can_id, is_ext), group in sorted(id_groups.items(), key=lambda kv: len(kv[1]), reverse=True):
        ts = [g.sof_time for g in group]
        intervals = [ts[i + 1] - ts[i] for i in range(len(ts) - 1)]
        avg_period_ms = mean(intervals) * 1000.0 if intervals else None
        jitter_abs = [abs(x - mean(intervals)) for x in intervals] if len(intervals) >= 2 else []
        p95_jitter_ms = percentile(jitter_abs, 95)
        fps = len(group) / duration_capture

        id_hex = f"0x{can_id:08X}" if is_ext else f"0x{can_id:03X}"
        cmd = ((can_id >> 24) & 0x1F) if is_ext else None

        id_rows.append(
            [
                id_hex,
                "EXT" if is_ext else "STD",
                len(group),
                f"{fps:.3f}",
                fmt_float(avg_period_ms, 3),
                fmt_float(p95_jitter_ms * 1000.0 if p95_jitter_ms is not None else None, 3),
                cmd_name(cmd),
            ]
        )

    report["id"] = id_rows

    # 命令统计（仅扩展帧）
    cmd_counter: Counter[int] = Counter()
    for f in frames_sorted:
        if f.is_extended and f.cmd_type is not None:
            cmd_counter[f.cmd_type] += 1

    cmd_rows: List[List[object]] = []
    for cmd, cnt in sorted(cmd_counter.items(), key=lambda kv: kv[1], reverse=True):
        cmd_rows.append([f"0x{cmd:02X}", cmd_name(cmd), cnt, f"{100.0 * cnt / total:.2f}%"])
    report["cmd"] = cmd_rows

    # 参数索引统计（READ/WRITE）
    rw_counter: Dict[int, Dict[str, int]] = defaultdict(lambda: {"read": 0, "write": 0})
    for f in frames_sorted:
        if not f.is_extended or f.cmd_type not in (0x11, 0x12):
            continue
        if len(f.data) < 2:
            continue
        idx = f.data[0] | (f.data[1] << 8)
        if f.cmd_type == 0x11:
            rw_counter[idx]["read"] += 1
        else:
            rw_counter[idx]["write"] += 1

    param_rows: List[List[object]] = []
    for idx, rw in sorted(rw_counter.items(), key=lambda kv: (kv[1]["read"] + kv[1]["write"]), reverse=True):
        param_rows.append(
            [
                f"0x{idx:04X}",
                ROBSTRIDE_PARAM_NAMES.get(idx, "UNKNOWN"),
                rw["read"],
                rw["write"],
                rw["read"] + rw["write"],
            ]
        )
    report["param"] = param_rows

    # 位域统计（扩展ID）
    ext_ids = [f.can_id for f in frames_sorted if f.is_extended]
    if ext_ids:
        cmd_vals = [(x >> 24) & 0x1F for x in ext_ids]
        f23_16 = [(x >> 16) & 0xFF for x in ext_ids]
        f15_8 = [(x >> 8) & 0xFF for x in ext_ids]
        f7_0 = [x & 0xFF for x in ext_ids]

        def top_vals(vals: Sequence[int], topn: int = 4) -> str:
            c = Counter(vals)
            return ", ".join([f"0x{k:02X}({v})" for k, v in c.most_common(topn)])

        bitfield_rows = [
            ["Bit[28:24]", "cmd_type", len(set(cmd_vals)), top_vals(cmd_vals)],
            ["Bit[23:16]", "status_or_data_hi", len(set(f23_16)), top_vals(f23_16)],
            ["Bit[15:8]", "data_lo_or_motor_id", len(set(f15_8)), top_vals(f15_8)],
            ["Bit[7:0]", "motor_or_host_id", len(set(f7_0)), top_vals(f7_0)],
        ]

        # 反馈帧专用状态位统计
        fb_ids = [x for x in ext_ids if ((x >> 24) & 0x1F) in (0x02, 0x18)]
        if fb_ids:
            mode_vals = [((x >> 22) & 0x03) for x in fb_ids]
            fault_vals = [((x >> 16) & 0x3F) for x in fb_ids]
            mode_count = Counter(mode_vals)
            mode_text = ", ".join([f"{m}:{c}" for m, c in sorted(mode_count.items())])

            bitfield_rows.append(["反馈位域", "mode_state(Bit[23:22])", len(mode_count), mode_text])

            total_fb = len(fault_vals)
            for b in range(6):
                set_cnt = sum(1 for v in fault_vals if (v >> b) & 1)
                ratio = 100.0 * set_cnt / total_fb if total_fb else 0.0
                bitfield_rows.append([f"Fault bit{b}", "Bit[16+b]", set_cnt, f"{ratio:.2f}%"])

        report["bitfield"] = bitfield_rows
    else:
        report["bitfield"] = []

    # 反馈物理量统计（自动识别可解码的关键参数）
    quality_frames = [
        f
        for f in frames_sorted
        if (f.crc_ok is not False) and (f.form_ok is not False) and (f.ack_dominant is not False)
    ]

    fb_by_motor: Dict[int, List[Tuple[float, float, float, float]]] = defaultdict(list)
    for f in quality_frames:
        if not (f.is_extended and f.cmd_type in (0x02, 0x18) and len(f.data) >= 8):
            continue
        motor_id = (f.can_id >> 8) & 0xFF
        pos_raw = (f.data[0] << 8) | f.data[1]
        vel_raw = (f.data[2] << 8) | f.data[3]
        tor_raw = (f.data[4] << 8) | f.data[5]
        tmp_raw = (f.data[6] << 8) | f.data[7]

        pos = uint16_to_float(pos_raw, ROBSTRIDE_P_MIN, ROBSTRIDE_P_MAX)
        vel = uint16_to_float(vel_raw, ROBSTRIDE_V_MIN, ROBSTRIDE_V_MAX)
        tor = uint16_to_float(tor_raw, ROBSTRIDE_T_MIN, ROBSTRIDE_T_MAX)
        tmp = tmp_raw / 10.0
        fb_by_motor[motor_id].append((pos, vel, tor, tmp))

    fb_rows: List[List[object]] = []
    for mid, vals in sorted(fb_by_motor.items(), key=lambda kv: len(kv[1]), reverse=True):
        pos_vals = [v[0] for v in vals]
        vel_vals = [abs(v[1]) for v in vals]
        tor_vals = [abs(v[2]) for v in vals]
        tmp_vals = [v[3] for v in vals]

        fb_rows.append(
            [
                mid,
                len(vals),
                fmt_float(min(pos_vals), 4),
                fmt_float(max(pos_vals), 4),
                fmt_float(percentile(vel_vals, 95), 4),
                fmt_float(percentile(tor_vals, 95), 4),
                fmt_float(mean(tmp_vals), 3),
                fmt_float(max(tmp_vals), 3),
            ]
        )
    report["feedback_stats"] = fb_rows

    # ========== 8电机+1传感器扩容预测 ==========
    ext8_bits = [f.sampled_bits for f in quality_frames if f.is_extended and f.payload_len == 8]
    mean_ext8_bits = mean_or_default(ext8_bits, 150.0)

    ctrl_bits = [
        f.sampled_bits
        for f in quality_frames
        if f.is_extended and (f.cmd_type is not None) and (f.cmd_type not in (0x02, 0x18))
    ]
    mean_ctrl_bits = mean_or_default(ctrl_bits, mean_ext8_bits)

    motor_fb_counter: Counter[int] = Counter()
    for f in quality_frames:
        if f.is_extended and f.cmd_type in (0x02, 0x18) and f.payload_len >= 8:
            motor_fb_id = (f.can_id >> 8) & 0xFF
            if motor_fb_id > 0:
                motor_fb_counter[motor_fb_id] += 1

    observed_active_motors = max(len(motor_fb_counter), 1)
    dominant_motor_id = 0
    dominant_motor_cnt = 0
    if motor_fb_counter:
        dominant_motor_id, dominant_motor_cnt = motor_fb_counter.most_common(1)[0]

    observed_motor_hz = (dominant_motor_cnt / duration_capture) if dominant_motor_cnt > 0 else 0.0
    used_motor_hz = proj_motor_hz_override if proj_motor_hz_override > 0 else observed_motor_hz

    non_feedback_ext_cnt = sum(
        1
        for f in quality_frames
        if f.is_extended and (f.cmd_type is not None) and (f.cmd_type not in (0x02, 0x18))
    )
    per_motor_ctrl_hz = (non_feedback_ext_cnt / duration_capture) / observed_active_motors

    def load_percent(motor_hz: float, sensor_hz: float) -> Tuple[float, float]:
        bps = (
            proj_motors * motor_hz * mean_ext8_bits
            + sensor_hz * mean_ext8_bits
            + proj_motors * per_motor_ctrl_hz * mean_ctrl_bits
        ) * proj_overhead
        return bps, (100.0 * bps / bitrate) if bitrate > 0 else 0.0

    bps_a, load_a = load_percent(used_motor_hz, proj_sensor_hz)
    bps_b, load_b = load_percent(proj_ref_feedback_hz, proj_sensor_hz)

    # 在当前假设下，70%占用率对应的单电机反馈频率上限
    max_motor_hz_70 = 0.0
    if bitrate > 0 and proj_motors > 0 and mean_ext8_bits > 0:
        reserved = (proj_sensor_hz * mean_ext8_bits + proj_motors * per_motor_ctrl_hz * mean_ctrl_bits) * proj_overhead
        budget = 0.7 * bitrate - reserved
        if budget > 0:
            max_motor_hz_70 = budget / (proj_motors * mean_ext8_bits * proj_overhead)

    report["projection_assumptions"] = [
        ["扩容目标", f"{proj_motors}电机 + 1传感器"],
        ["平均扩展8B帧长(bit)", f"{mean_ext8_bits:.3f}"],
        ["平均控制帧长(bit)", f"{mean_ctrl_bits:.3f}"],
        ["观测活跃电机数", observed_active_motors],
        ["观测主电机ID", dominant_motor_id],
        ["观测主电机反馈频率(Hz)", f"{observed_motor_hz:.3f}"],
        ["每电机控制指令频率估计(Hz)", f"{per_motor_ctrl_hz:.6f}"],
        ["扩容建模口径", "仅使用CRC/格式/ACK通过帧"],
        ["开销系数(overhead)", f"{proj_overhead:.3f}"],
    ]

    report["projection"] = [
        [
            "场景A:按当前实测反馈频率扩容",
            f"{used_motor_hz:.3f}",
            f"{proj_sensor_hz:.3f}",
            f"{bps_a:.3f}",
            f"{load_a:.3f}%",
            "OK" if load_a < 70.0 else "风险",
        ],
        [
            "场景B:参考10ms反馈(100Hz)扩容",
            f"{proj_ref_feedback_hz:.3f}",
            f"{proj_sensor_hz:.3f}",
            f"{bps_b:.3f}",
            f"{load_b:.3f}%",
            "OK" if load_b < 70.0 else "风险",
        ],
        [
            "70%占用率下单电机反馈频率上限",
            f"{max_motor_hz_70:.3f}",
            "-",
            "-",
            "70%",
            "容量边界",
        ],
    ]

    risks: List[List[object]] = []
    crc_err_pct = (100.0 * crc_fail / crc_checked) if crc_checked else 0.0
    if crc_err_pct > 1.0:
        risks.append(["链路完整性", "CRC错误率偏高", f"{crc_err_pct:.4f}%", "建议检查采样点、终端阻抗、共地与线缆干扰"]) 
    else:
        risks.append(["链路完整性", "CRC错误率可接受", f"{crc_err_pct:.4f}%", "保持现有物理层配置并持续监测"]) 

    if ack_missing > 0:
        risks.append(["总线确认", "存在ACK缺失", ack_missing, "检查接收节点在线状态与收发器工作模式"]) 
    else:
        risks.append(["总线确认", "未见ACK缺失", 0, "确认工作正常"]) 

    if candidate_noise_rate >= 0.7:
        risks.append(["候选噪声", "SOF候选误触发较多", f"{100.0 * candidate_noise_rate:.3f}%", "建议提高SOF过滤门限或使用Logic2 CAN解析导出交叉验证"]) 
    else:
        risks.append(["候选噪声", "候选噪声可控", f"{100.0 * candidate_noise_rate:.3f}%", "维持当前候选过滤参数"]) 

    if frame_net_error_rate >= 0.1:
        risks.append(["帧级质量", "净错误率偏高", f"{100.0 * frame_net_error_rate:.3f}%", "优先优化采样点与物理层布线，必要时降低总线速率"]) 
    else:
        risks.append(["帧级质量", "净错误率可接受", f"{100.0 * frame_net_error_rate:.3f}%", "可进入扩容验证阶段"]) 

    if load_b >= 70.0:
        risks.append(["扩容容量", "10ms扩容场景接近/超过70%", f"{load_b:.3f}%", "降低上报频率或拆分总线"]) 
    else:
        risks.append(["扩容容量", "10ms扩容场景有余量", f"{load_b:.3f}%", "容量可用，建议预留20%余量"]) 

    report["risk"] = risks

    return report


def write_outputs(
    out_prefix: Path,
    report: Dict[str, List[List[object]]],
    parsed_rows: Sequence[Dict[str, object]],
) -> Tuple[Path, Path]:
    md_path = out_prefix.with_suffix(".md")
    csv_path = out_prefix.with_suffix(".frames.csv")

    lines: List[str] = []
    lines.append("# Logic2 CAN 自动分析报告")
    lines.append("")

    lines.append("## CAN 性能表")
    lines.append(build_markdown_table(["指标", "数值"], report.get("perf", [])))
    lines.append("")

    lines.append("## 通信质量与错误率")
    lines.append(build_markdown_table(["指标", "数值"], report.get("quality", [])))
    lines.append("")

    lines.append("## 候选层与帧层双口径误码模型")
    lines.append(build_markdown_table(["口径", "BER下界", "FER", "说明"], report.get("error_model", [])))
    lines.append("")

    lines.append("## 解码失败原因分布")
    lines.append(build_markdown_table(["原因", "次数"], report.get("errors", [])))
    lines.append("")

    lines.append("## CAN ID 统计表")
    lines.append(build_markdown_table(["CAN ID", "类型", "帧数", "平均帧率", "平均周期(ms)", "P95抖动(us)", "主命令"], report.get("id", [])))
    lines.append("")

    lines.append("## 命令统计表")
    lines.append(build_markdown_table(["命令", "名称", "帧数", "占比"], report.get("cmd", [])))
    lines.append("")

    lines.append("## 参数索引统计表")
    lines.append(build_markdown_table(["参数Index", "参数名", "READ次数", "WRITE次数", "合计"], report.get("param", [])))
    lines.append("")

    lines.append("## 位域统计表")
    lines.append(build_markdown_table(["位域", "语义", "唯一值数", "主要取值/计数"], report.get("bitfield", [])))
    lines.append("")

    lines.append("## 自动关键参数统计（反馈物理量）")
    lines.append(build_markdown_table(["电机ID", "样本数", "位置最小(rad)", "位置最大(rad)", "|速度|P95(rad/s)", "|力矩|P95(Nm)", "温度均值(°C)", "温度最大(°C)"], report.get("feedback_stats", [])))
    lines.append("")

    lines.append("## 扩容假设参数")
    lines.append(build_markdown_table(["参数", "取值"], report.get("projection_assumptions", [])))
    lines.append("")

    lines.append("## 8电机+1传感器扩容预测")
    lines.append(build_markdown_table(["场景", "电机反馈频率(Hz)", "传感器频率(Hz)", "估算带宽(bps)", "总线占用", "结论"], report.get("projection", [])))
    lines.append("")

    lines.append("## 风险评估建议")
    lines.append(build_markdown_table(["维度", "结论", "指标", "建议"], report.get("risk", [])))
    lines.append("")

    md_path.write_text("\n".join(lines), encoding="utf-8")

    # 明细帧导出
    if parsed_rows:
        fieldnames: List[str] = []
        keys = set()
        for row in parsed_rows:
            keys.update(row.keys())
        # 固定优先顺序
        ordered = [
            "timestamp_s",
            "id_hex",
            "is_extended",
            "is_fd",
            "dlc",
            "payload_len",
            "cmd_type",
            "cmd_name",
            "data_hex",
        ]
        fieldnames.extend([k for k in ordered if k in keys])
        fieldnames.extend(sorted(k for k in keys if k not in fieldnames))

        with csv_path.open("w", encoding="utf-8-sig", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            for row in parsed_rows:
                writer.writerow(row)
    else:
        csv_path.write_text("", encoding="utf-8")

    return md_path, csv_path


def print_report(report: Dict[str, List[List[object]]]) -> None:
    print("\n===== CAN 性能表 =====")
    for k, v in report.get("perf", []):
        print(f"{k:<28}: {v}")

    print("\n===== 通信质量与错误率 =====")
    for k, v in report.get("quality", []):
        print(f"{k:<28}: {v}")

    print("\n===== 候选层/帧层双口径误码 =====")
    for row in report.get("error_model", []):
        print(f"{row[0]:<20} BER_lb={row[1]} FER={row[2]} ({row[3]})")

    print("\n===== 解码失败原因 =====")
    err_rows = report.get("errors", [])
    if not err_rows:
        print("无")
    else:
        for row in err_rows[:10]:
            print(f"{row[0]:<24}: {row[1]}")

    print("\n===== 命令统计（Top） =====")
    cmd_rows = report.get("cmd", [])
    if not cmd_rows:
        print("无")
    else:
        for row in cmd_rows[:10]:
            print(f"{row[0]:>6}  {row[1]:<16}  count={row[2]:>6}  ratio={row[3]}")

    print("\n===== 参数索引统计（Top） =====")
    param_rows = report.get("param", [])
    if not param_rows:
        print("无 READ/WRITE 参数帧")
    else:
        for row in param_rows[:15]:
            print(f"{row[0]}  {row[1]:<16}  R={row[2]:>4}  W={row[3]:>4}  SUM={row[4]:>4}")

    print("\n===== 8电机+1传感器扩容预测 =====")
    proj_rows = report.get("projection", [])
    if not proj_rows:
        print("无")
    else:
        for row in proj_rows:
            print(
                f"{row[0]} | motor={row[1]}Hz | sensor={row[2]}Hz | "
                f"bw={row[3]}bps | load={row[4]} | {row[5]}"
            )

    print("\n===== 自动关键参数统计（Top） =====")
    fb_rows = report.get("feedback_stats", [])
    if not fb_rows:
        print("无反馈物理量样本")
    else:
        for row in fb_rows[:8]:
            print(
                f"motor={row[0]} samples={row[1]} pos=[{row[2]}, {row[3]}] "
                f"|v|P95={row[4]} |t|P95={row[5]} Tavg={row[6]} Tmax={row[7]}"
            )

    print("\n===== 风险评估建议 =====")
    for row in report.get("risk", []):
        print(f"[{row[0]}] {row[1]} (指标={row[2]}) -> {row[3]}")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Logic2 raw digital CAN 自动分析")
    p.add_argument("csv_path", help="Logic2 导出的 raw digital CSV（Time [s], Channel X）")
    p.add_argument("--bitrate", type=float, default=0.0, help="手动指定仲裁位率（bps），例如 1000000")
    p.add_argument("--sample-point", type=float, default=0.8, help="位采样点（0~1），默认 0.8")
    p.add_argument("--fixed-sample-point", action="store_true", help="手动bitrate模式下固定采样点，不自动扫描")
    p.add_argument("--sample-vote-count", type=int, default=1, help="位内投票采样点数（奇数，默认1=单点）")
    p.add_argument("--sample-vote-span", type=float, default=0.0, help="位内投票采样扩展范围（位时间比例，默认0）")
    p.add_argument("--frame-sp-retries", type=int, default=0, help="单帧失败时采样点重试次数（默认0）")
    p.add_argument("--frame-sp-step", type=float, default=0.02, help="单帧采样点重试步长（默认0.02）")
    p.add_argument("--disable-bit-sync", action="store_true", help="禁用位同步重同步（默认启用）")
    p.add_argument("--sync-window", type=float, default=0.35, help="重同步边沿搜索窗口（位时间比例，默认0.35）")
    p.add_argument("--sync-sjw", type=float, default=0.20, help="重同步SJW上限（位时间比例，默认0.20）")
    p.add_argument("--sync-min-hold", type=float, default=0.20, help="可用于同步的边沿后最小保持（位时间比例，默认0.20）")
    p.add_argument("--sync-on-any-edge", action="store_true", help="允许任意方向边沿参与重同步（默认仅对齐dominant边沿）")
    p.add_argument("--sof-idle-bits", type=int, default=11, help="SOF判定所需最小空闲位数（默认11）")
    p.add_argument("--sof-min-hold", type=float, default=0.55, help="SOF首个dominant段最小保持位数（默认0.55）")
    p.add_argument("--sof-min-edges", type=int, default=2, help="SOF后窗口最小边沿数过滤（默认2）")
    p.add_argument("--sof-edge-window-bits", type=int, default=20, help="SOF后边沿统计窗口位数（默认20）")
    p.add_argument("--glitch-us", type=float, default=0.35, help="毛刺阈值（us），短于该值且前后同电平将合并")
    p.add_argument("--proj-motors", type=int, default=8, help="扩容评估中的电机数量（默认8）")
    p.add_argument("--proj-sensor-hz", type=float, default=100.0, help="扩容评估中的传感器上报频率Hz（默认100）")
    p.add_argument("--proj-motor-hz", type=float, default=0.0, help="扩容评估中单电机反馈频率Hz，0表示按实测主电机自动估计")
    p.add_argument("--proj-overhead", type=float, default=1.10, help="扩容评估链路开销系数（仲裁重传/抖动余量），默认1.10")
    p.add_argument("--proj-ref-feedback-hz", type=float, default=100.0, help="参考场景的单电机反馈频率Hz（默认100，对应10ms）")
    p.add_argument("--out-prefix", default="", help="输出前缀，不填则默认在输入同目录生成 *_analysis")
    return p.parse_args()


def main() -> None:
    args = parse_args()

    csv_path = Path(args.csv_path).expanduser().resolve()
    if not csv_path.exists():
        raise FileNotFoundError(f"找不到文件: {csv_path}")

    if args.sample_point <= 0.0 or args.sample_point >= 1.0:
        raise ValueError("--sample-point 必须在 (0,1) 内")
    if args.sof_idle_bits < 1:
        raise ValueError("--sof-idle-bits 必须 >= 1")
    if args.sof_min_hold <= 0:
        raise ValueError("--sof-min-hold 必须 > 0")
    if args.sof_min_edges < 0:
        raise ValueError("--sof-min-edges 必须 >= 0")
    if args.sof_edge_window_bits < 1:
        raise ValueError("--sof-edge-window-bits 必须 >= 1")
    if args.sample_vote_count < 1:
        raise ValueError("--sample-vote-count 必须 >= 1")
    if args.sample_vote_count % 2 == 0:
        raise ValueError("--sample-vote-count 必须为奇数")
    if args.sample_vote_span < 0.0 or args.sample_vote_span > 0.3:
        raise ValueError("--sample-vote-span 必须在 [0, 0.3] 内")
    if args.frame_sp_retries < 0 or args.frame_sp_retries > 6:
        raise ValueError("--frame-sp-retries 必须在 [0, 6] 内")
    if args.frame_sp_step < 0.0 or args.frame_sp_step > 0.1:
        raise ValueError("--frame-sp-step 必须在 [0, 0.1] 内")
    if args.sync_window < 0.0 or args.sync_window >= 0.5:
        raise ValueError("--sync-window 必须在 [0, 0.5) 内")
    if args.sync_sjw < 0.0 or args.sync_sjw > 0.5:
        raise ValueError("--sync-sjw 必须在 [0, 0.5] 内")
    if args.sync_min_hold < 0.0 or args.sync_min_hold > 1.0:
        raise ValueError("--sync-min-hold 必须在 [0, 1] 内")

    print(f"读取: {csv_path}")
    times, levels = read_logic2_edge_csv(csv_path)
    print(f"有效边沿数: {len(times)}")

    segments = build_segments(times, levels)

    auto_bt = estimate_bit_time(segments)
    manual_bt = (1.0 / args.bitrate) if args.bitrate and args.bitrate > 0 else None
    used_bt = manual_bt if manual_bt is not None else auto_bt
    sample_cfg = BitSamplingConfig(
        vote_count=args.sample_vote_count,
        vote_span=args.sample_vote_span,
        frame_sp_retries=args.frame_sp_retries,
        frame_sp_step=args.frame_sp_step,
    )
    sample_vote_offsets = build_vote_offsets(sample_cfg.vote_count, sample_cfg.vote_span)
    frame_sp_offsets = build_frame_sp_offsets(sample_cfg.frame_sp_retries, sample_cfg.frame_sp_step)
    sync_cfg = BitSyncConfig(
        enabled=not args.disable_bit_sync,
        window=args.sync_window,
        sjw=args.sync_sjw,
        min_hold=args.sync_min_hold,
        on_any_edge=args.sync_on_any_edge,
    )

    glitch_th = max(args.glitch_us * 1e-6, 0.0)
    segments_filtered = filter_glitches(segments, glitch_th) if glitch_th > 0 else segments

    print(f"自动估计仲裁位时间: {auto_bt * 1e6:.3f} us ({1.0 / auto_bt / 1000.0:.3f} kbps)")
    if manual_bt is not None:
        print(f"手动指定仲裁位时间: {used_bt * 1e6:.3f} us ({1.0 / used_bt / 1000.0:.3f} kbps)")
    print(
        "位同步模式: "
        + ("ON" if sync_cfg.enabled else "OFF")
        + f" (window={sync_cfg.window:.3f}, sjw={sync_cfg.sjw:.3f}, "
        + f"min_hold={sync_cfg.min_hold:.3f}, any_edge={int(sync_cfg.on_any_edge)})"
    )
    print(
        f"采样策略: vote_count={sample_cfg.vote_count}, vote_span={sample_cfg.vote_span:.3f}, "
        + f"frame_sp_retries={sample_cfg.frame_sp_retries}, frame_sp_step={sample_cfg.frame_sp_step:.3f}"
    )

    if manual_bt is None:
        used_bt, used_sp, frames, attempts, sof_total = choose_best_timing(
            segments_filtered,
            auto_bt,
            args.sample_point,
            sof_idle_bits=args.sof_idle_bits,
            sof_min_hold=args.sof_min_hold,
            sof_min_edges=args.sof_min_edges,
            sof_edge_window_bits=args.sof_edge_window_bits,
            sync_cfg=sync_cfg,
            sample_vote_offsets=sample_vote_offsets,
            frame_sp_offsets=frame_sp_offsets,
        )
    else:
        if args.fixed_sample_point:
            used_sp = args.sample_point
            frames, attempts, sof_total = decode_frames(
                segments_filtered,
                used_bt,
                sample_point=used_sp,
                max_candidates=None,
                sof_idle_bits=args.sof_idle_bits,
                sof_min_hold=args.sof_min_hold,
                sof_min_edges=args.sof_min_edges,
                sof_edge_window_bits=args.sof_edge_window_bits,
                sync_cfg=sync_cfg,
                sample_vote_offsets=sample_vote_offsets,
                frame_sp_offsets=frame_sp_offsets,
            )
        else:
            used_sp, frames, attempts, sof_total = choose_best_sample_point(
                segments_filtered,
                used_bt,
                args.sample_point,
                sof_idle_bits=args.sof_idle_bits,
                sof_min_hold=args.sof_min_hold,
                sof_min_edges=args.sof_min_edges,
                sof_edge_window_bits=args.sof_edge_window_bits,
                sync_cfg=sync_cfg,
                sample_vote_offsets=sample_vote_offsets,
                frame_sp_offsets=frame_sp_offsets,
            )

    print(f"最终采用仲裁位时间: {used_bt * 1e6:.3f} us ({1.0 / used_bt / 1000.0:.3f} kbps)")
    print(f"最终采用采样点: {used_sp:.3f}")
    print(f"解码帧数: {len(frames)}")

    capture_start = times[0]
    capture_end = times[-1]
    report = summarize(
        frames=frames,
        attempts=attempts,
        sof_total=sof_total,
        capture_start=capture_start,
        capture_end=capture_end,
        bit_time=used_bt,
        sample_point=used_sp,
        proj_motors=max(args.proj_motors, 1),
        proj_sensor_hz=max(args.proj_sensor_hz, 0.0),
        proj_motor_hz_override=max(args.proj_motor_hz, 0.0),
        proj_overhead=max(args.proj_overhead, 1.0),
        proj_ref_feedback_hz=max(args.proj_ref_feedback_hz, 0.0),
    )

    parsed_rows = [parse_frame_fields(f) for f in sorted(frames, key=lambda x: x.sof_time)]

    if args.out_prefix:
        out_prefix = Path(args.out_prefix).expanduser().resolve()
    else:
        out_prefix = csv_path.with_name(csv_path.stem + "_analysis")

    md_path, frames_csv_path = write_outputs(out_prefix, report, parsed_rows)
    print_report(report)

    print("\n输出文件:")
    print(f"- 报告: {md_path}")
    print(f"- 帧明细: {frames_csv_path}")


if __name__ == "__main__":
    main()
