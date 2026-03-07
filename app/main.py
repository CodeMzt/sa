import flet as ft
import socket
import json
import threading
import time
import struct
import subprocess
import re

# ============================================================
#  全局状态
# ============================================================
tcp_client: socket.socket | None = None
is_connected = False

# 颜色主题
ACCENT = "#00BCD4"       # 青色主色调
ACCENT_DIM = "#00838F"
BG_CARD = "#1E1E2E"
BG_LOG = "#121220"
TEXT_DIM = "#90A4AE"


def main(page: ft.Page):
    # ---- 页面基本设置 ----
    page.title = "机械臂 调试控制台"
    page.theme_mode = ft.ThemeMode.DARK
    page.bgcolor = "#0D0D1A"
    page.padding = ft.Padding(left=16, right=16, top=36, bottom=10)
    page.fonts = {"HarmonyOS": "https://s1.hdslb.com/bfs/static/jinkela/long/font/regular.css"}
    is_mobile = (
        page.platform in (ft.PagePlatform.ANDROID, ft.PagePlatform.IOS)
        or ((page.width or 0) > 0 and (page.width or 0) < 520)
    )

    # ---- 连接状态指示 ----
    status_icon = ft.Icon(ft.Icons.CANCEL, color=ft.Colors.RED_400, size=14)
    status_text = ft.Text("未连接", size=12, color=TEXT_DIM)

    def update_status():
        if is_connected:
            status_icon.name = ft.Icons.CHECK_CIRCLE
            status_icon.color = ft.Colors.GREEN_400
            status_text.value = "已连接"
            status_text.color = ft.Colors.GREEN_400
            connect_btn.text = "断开连接"
            connect_btn.icon = ft.Icons.LINK_OFF
            connect_btn.bgcolor = "#B71C1C"
        else:
            status_icon.name = ft.Icons.CANCEL
            status_icon.color = ft.Colors.RED_400
            status_text.value = "未连接"
            status_text.color = TEXT_DIM
            connect_btn.text = "连接设备"
            connect_btn.icon = ft.Icons.WIFI
            connect_btn.bgcolor = ACCENT
        page.update()

    def blur(_=None):
        page.focus(None)

    def detect_hotspot_gateway_ip():
        """尝试从系统网络配置中识别热点网关 IP（通常即设备 IP）。"""
        def _extract_gateway(block_text):
            lines = block_text.splitlines()
            for i, line in enumerate(lines):
                if ("Default Gateway" in line) or ("默认网关" in line):
                    m = re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", line)
                    if m:
                        return m.group(0)
                    for j in range(i + 1, min(i + 3, len(lines))):
                        m2 = re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", lines[j])
                        if m2:
                            return m2.group(0)
            return None

        def _is_private_ipv4(ip):
            if ip.startswith("10.") or ip.startswith("192.168."):
                return True
            if ip.startswith("172."):
                parts = ip.split(".")
                if len(parts) > 1 and parts[1].isdigit():
                    return 16 <= int(parts[1]) <= 31
            return False

        for enc in ("gbk", "utf-8"):
            try:
                out = subprocess.check_output("ipconfig", encoding=enc, errors="ignore")
                break
            except Exception:
                out = ""
        if not out:
            return None

        blocks = re.split(r"\r?\n\r?\n+", out)
        wifi_gws = []
        other_gws = []

        for block in blocks:
            gw = _extract_gateway(block)
            if not gw:
                continue
            block_lower = block.lower()
            is_wifi = (
                ("wi-fi" in block_lower)
                or ("wifi" in block_lower)
                or ("wireless" in block_lower)
                or ("wlan" in block_lower)
                or ("无线" in block)
            )
            if is_wifi:
                wifi_gws.append(gw)
            else:
                other_gws.append(gw)

        for arr in (wifi_gws, other_gws):
            for ip in arr:
                if _is_private_ipv4(ip):
                    return ip
        if wifi_gws:
            return wifi_gws[0]
        if other_gws:
            return other_gws[0]
        return None

    # ---- 反馈条（浮动覆盖层，不影响布局） ----
    feedback_text = ft.Text("", size=13, color="white", weight=ft.FontWeight.W_600)
    feedback_bar = ft.Container(
        content=feedback_text,
        bgcolor="#2E7D32", border_radius=20,
        padding=ft.Padding(left=20, right=20, top=9, bottom=9),
        visible=False, animate_opacity=300,
    )

    def toast(msg, color="#66BB6A"):
        """显示浮动反馈提示，2.5s 后自动隐藏"""
        print(f"[TOAST] {msg}")
        feedback_text.value = msg
        feedback_bar.bgcolor = color
        feedback_bar.visible = True
        page.update()
        def _hide():
            time.sleep(2.5)
            feedback_bar.visible = False
            page.update()
        page.run_thread(_hide)

    # ---- UI 组件 ----
    log_text = ft.TextField(
        multiline=True,
        read_only=True,
        value="",
        expand=True,
        text_size=12,
        border_radius=8,
        border_color=BG_LOG,
        bgcolor=BG_LOG,
        color="#CFD8DC",
        cursor_color=ACCENT,
        min_lines=10,
        max_lines=1000
    )
    conn_log_text = ft.TextField(
        multiline=True,
        read_only=True,
        value="",
        expand=True,
        text_size=12,
        border_radius=8,
        border_color=BG_LOG,
        bgcolor=BG_LOG,
        color="#CFD8DC",
        cursor_color=ACCENT,
        min_lines=10,
        max_lines=1000
    )
    ip_input = ft.TextField(
        value="192.168.4.1", label="设备 IP", 
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
        text_size=14, label_style=ft.TextStyle(size=12, color=TEXT_DIM),
        expand=True, prefix_icon=ft.Icons.ROUTER,
        on_submit=blur
    )
    port_input = ft.TextField(
        value="8080", label="端口",
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
        text_size=14, label_style=ft.TextStyle(size=12, color=TEXT_DIM),
        width=110, prefix_icon=ft.Icons.TAG,
        on_submit=blur
    )

    # 系统配置字段 (所有 expand=True 防宽度溢出)
    dev_ip = ft.TextField(label="本机 IP", value="192.168.4.1", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    dev_mask = ft.TextField(label="子网掩码", value="255.255.255.0", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    dev_gw = ft.TextField(label="网关", value="192.168.4.1", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    srv_ip = ft.TextField(label="上位机 IP", value="192.168.4.2", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    srv_port = ft.TextField(label="上位机端口", value="9000", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    dbg_port = ft.TextField(label="调试端口", value="8080", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    wifi_ssid = ft.TextField(label="WiFi SSID", value="SurgicalArm_Debug", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)
    wifi_psk = ft.TextField(label="WiFi 密码", value="fdudebug", password=True, can_reveal_password=True, border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)

    LOG_PAGE_SIZE = 256  # 每页 256 字节
    log_pages_input = ft.TextField(
        label="页数(256B/页)", value="4", width=160,
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
        text_size=12, on_submit=blur,
    )
    log_auto_stop = ft.Checkbox(label="遇空白自动停止", value=True,
                                label_style=ft.TextStyle(size=12, color=TEXT_DIM))
    _log_reading = {"active": False, "buf": b"", "page": 0, "max_pages": 4}  # 多页读取状态

    # 运动配置显示与编辑
    FRAME_SIZE = 4 + 4 + 4 + 4 + 2 + 1
    MAX_FRAMES = 10
    GROUP_SIZE = 4 + 16 + MAX_FRAMES * FRAME_SIZE
    TOTAL_ACTION_GROUPS = 7
    MOTION_RAW_SIZE = TOTAL_ACTION_GROUPS * GROUP_SIZE + 4

    motion_data_view = ft.ListView(expand=True, spacing=2, auto_scroll=False)
    ACTION_NAMES = {0: "移动", 1: "抓取", 2: "释放"}

    motion_write_supported = {"ok": True}
    selected_group_idx = {"value": 0}
    selected_frame_idx = {"value": 0}
    suspend_editor_events = {"value": False}

    def _blank_motion_model():
        return {
            "groups": [
                {"name": "", "frames": []}
                for _ in range(TOTAL_ACTION_GROUPS)
            ]
        }

    motion_model = _blank_motion_model()

    motion_group_dd = ft.Dropdown(
        label="动作组", value="0", width=120,
        options=[ft.dropdown.Option(str(i), f"G{i}") for i in range(TOTAL_ACTION_GROUPS)],
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
    )
    motion_frame_dd = ft.Dropdown(
        label="帧", value="0", width=120,
        options=[ft.dropdown.Option("0", "帧0")],
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
    )
    motion_group_name = ft.TextField(
        label="组名", value="", expand=True,
        border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur
    )
    frame_m1 = ft.TextField(label="M1(°)", value="0", expand=True, border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur)
    frame_m2 = ft.TextField(label="M2(°)", value="0", expand=True, border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur)
    frame_m3 = ft.TextField(label="M3(°)", value="0", expand=True, border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur)
    frame_m4 = ft.TextField(label="M4(°)", value="0", expand=True, border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur)
    frame_dur = ft.TextField(label="耗时(ms)", value="500", width=140, border_color=ACCENT_DIM, focused_border_color=ACCENT, on_submit=blur)
    frame_act = ft.Dropdown(
        label="动作", value="0", width=140,
        options=[
            ft.dropdown.Option("0", "移动"),
            ft.dropdown.Option("1", "抓取"),
            ft.dropdown.Option("2", "释放"),
        ],
        border_color=ACCENT_DIM, focused_border_color=ACCENT,
    )
    motion_add_frame_btn = ft.Button("新增帧", icon=ft.Icons.ADD)
    motion_del_frame_btn = ft.Button("删除帧", icon=ft.Icons.REMOVE)
    motion_save_btn = ft.Button("保存到设备", icon=ft.Icons.SAVE)

    if is_mobile:
        port_input.width = None
        port_input.expand = True
        motion_group_dd.width = None
        motion_group_dd.expand = True
        motion_frame_dd.width = None
        motion_frame_dd.expand = True
        frame_dur.width = None
        frame_dur.expand = True
        frame_act.width = None
        frame_act.expand = True

    def _safe_float(s, default=0.0):
        try:
            return float(s)
        except Exception:
            return default

    def _safe_int(s, default=0):
        try:
            return int(float(s))
        except Exception:
            return default

    def _motion_model_from_hex(hex_str):
        try:
            raw = bytes.fromhex(hex_str)
        except Exception:
            raise ValueError("invalid_hex")

        if len(raw) < MOTION_RAW_SIZE:
            raw = raw + (b"\x00" * (MOTION_RAW_SIZE - len(raw)))
        else:
            raw = raw[:MOTION_RAW_SIZE]

        model = _blank_motion_model()
        for g in range(TOTAL_ACTION_GROUPS):
            off = g * GROUP_SIZE
            group_raw = raw[off:off + GROUP_SIZE]
            if len(group_raw) < GROUP_SIZE:
                continue

            if all(b == 0xFF for b in group_raw):
                continue

            frame_count = struct.unpack_from('<I', raw, off)[0]
            name_bytes = raw[off + 4:off + 20]
            try:
                name = name_bytes.split(b'\x00')[0].decode('utf-8', errors='replace')
            except Exception:
                name = ""

            model["groups"][g]["name"] = name
            fc = min(int(frame_count), MAX_FRAMES)
            for f in range(fc):
                foff = off + 20 + f * FRAME_SIZE
                if foff + FRAME_SIZE > len(raw):
                    break
                m1, m2, m3, m4 = struct.unpack_from('<ffff', raw, foff)
                dur = struct.unpack_from('<H', raw, foff + 16)[0]
                act = raw[foff + 18]
                model["groups"][g]["frames"].append({
                    "m1": float(m1), "m2": float(m2), "m3": float(m3), "m4": float(m4),
                    "duration_ms": int(dur), "action": int(act),
                })
        return model

    def _motion_model_to_hex(model):
        raw = bytearray(MOTION_RAW_SIZE)
        for g in range(TOTAL_ACTION_GROUPS):
            group = model["groups"][g]
            frames = group.get("frames", [])[:MAX_FRAMES]
            off = g * GROUP_SIZE

            struct.pack_into('<I', raw, off, len(frames))

            name = (group.get("name") or "").encode('utf-8', errors='ignore')[:15]
            raw[off + 4:off + 20] = b"\x00" * 16
            raw[off + 4:off + 4 + len(name)] = name

            for f, frame in enumerate(frames):
                foff = off + 20 + f * FRAME_SIZE
                struct.pack_into('<ffff', raw, foff,
                                 float(frame.get("m1", 0.0)),
                                 float(frame.get("m2", 0.0)),
                                 float(frame.get("m3", 0.0)),
                                 float(frame.get("m4", 0.0)))
                struct.pack_into('<H', raw, foff + 16, max(0, min(65535, int(frame.get("duration_ms", 0)))))
                raw[foff + 18] = max(0, min(255, int(frame.get("action", 0))))

        return raw.hex().upper()

    def refresh_motion_view():
        motion_data_view.controls.clear()
        for g in range(TOTAL_ACTION_GROUPS):
            group = motion_model["groups"][g]
            frames = group["frames"]
            name = (group.get("name") or "").strip()

            if len(frames) == 0 and not name:
                motion_data_view.controls.append(
                    ft.Text(f"G{g}: ∅ 未写入", size=11, color=TEXT_DIM, italic=True)
                )
                continue

            name_display = f'"{name}"' if name else '(未命名)'
            motion_data_view.controls.append(
                ft.Text(f"G{g}: {name_display}  ⟨{len(frames)}帧⟩", size=12, weight=ft.FontWeight.BOLD, color=ACCENT)
            )
            motion_data_view.controls.append(ft.Divider(height=1, color="#333"))

            for f, frame in enumerate(frames):
                act_name = ACTION_NAMES.get(int(frame.get("action", 0)), f"动作{frame.get('action', 0)}")
                angles_text = (
                    f"M1:{float(frame.get('m1', 0)):7.1f}°  M2:{float(frame.get('m2', 0)):7.1f}°  "
                    f"M3:{float(frame.get('m3', 0)):7.1f}°  M4:{float(frame.get('m4', 0)):7.1f}°"
                )
                duration_text = f"耗时 {int(frame.get('duration_ms', 0))}ms  ┃  {act_name}"
                motion_data_view.controls.append(
                    ft.Container(
                        content=ft.Column([
                            ft.Text(f"  帧{f}  {angles_text}", size=10, color="#CFD8DC", font_family="monospace"),
                            ft.Text(f"       {duration_text}", size=10, color="#90A4AE"),
                        ], spacing=2),
                        padding=ft.Padding(top=6, bottom=6, left=8, right=8),
                        bgcolor="#1A1A2E",
                        border_radius=4,
                        margin=ft.Margin(left=0, right=0, top=2, bottom=2),
                    )
                )

    def decode_motion_hex(hex_str):
        nonlocal motion_model
        try:
            motion_model = _motion_model_from_hex(hex_str)
        except Exception:
            toast("运动配置 hex 解码失败", "#C62828")
            return
        refresh_motion_view()
        _refresh_motion_editor()
        page.update()

    def _set_motion_editor_enabled(enabled, has_frame):
        motion_group_dd.disabled = not enabled
        motion_frame_dd.disabled = not enabled or not has_frame
        motion_group_name.disabled = not enabled
        frame_m1.disabled = not enabled or not has_frame
        frame_m2.disabled = not enabled or not has_frame
        frame_m3.disabled = not enabled or not has_frame
        frame_m4.disabled = not enabled or not has_frame
        frame_dur.disabled = not enabled or not has_frame
        frame_act.disabled = not enabled or not has_frame
        motion_add_frame_btn.disabled = not enabled
        motion_del_frame_btn.disabled = not enabled or not has_frame
        motion_save_btn.disabled = (not enabled) or (not motion_write_supported["ok"])

    def _refresh_frame_dropdown(group_idx):
        frames = motion_model["groups"][group_idx]["frames"]
        if not frames:
            motion_frame_dd.options = [ft.dropdown.Option("0", "帧0")]
            selected_frame_idx["value"] = 0
            motion_frame_dd.value = "0"
            return
        motion_frame_dd.options = [
            ft.dropdown.Option(str(i), f"帧{i}")
            for i in range(len(frames))
        ]
        selected_frame_idx["value"] = min(selected_frame_idx["value"], len(frames) - 1)
        motion_frame_dd.value = str(selected_frame_idx["value"])

    def _refresh_motion_editor():
        suspend_editor_events["value"] = True
        g_idx = max(0, min(TOTAL_ACTION_GROUPS - 1, selected_group_idx["value"]))
        selected_group_idx["value"] = g_idx
        group = motion_model["groups"][g_idx]
        frames = group["frames"]

        motion_group_dd.value = str(g_idx)
        motion_group_name.value = group.get("name", "")

        _refresh_frame_dropdown(g_idx)
        has_frame = len(frames) > 0
        if has_frame:
            f_idx = max(0, min(len(frames) - 1, selected_frame_idx["value"]))
            selected_frame_idx["value"] = f_idx
            frame = frames[f_idx]
            frame_m1.value = f"{float(frame.get('m1', 0.0)):.2f}"
            frame_m2.value = f"{float(frame.get('m2', 0.0)):.2f}"
            frame_m3.value = f"{float(frame.get('m3', 0.0)):.2f}"
            frame_m4.value = f"{float(frame.get('m4', 0.0)):.2f}"
            frame_dur.value = str(int(frame.get("duration_ms", 0)))
            frame_act.value = str(int(frame.get("action", 0)))
        else:
            frame_m1.value = "0"
            frame_m2.value = "0"
            frame_m3.value = "0"
            frame_m4.value = "0"
            frame_dur.value = "500"
            frame_act.value = "0"

        _set_motion_editor_enabled(True, has_frame)
        suspend_editor_events["value"] = False

    def _commit_motion_editor_to_model():
        if suspend_editor_events["value"]:
            return
        g_idx = selected_group_idx["value"]
        if g_idx < 0 or g_idx >= TOTAL_ACTION_GROUPS:
            return

        group = motion_model["groups"][g_idx]
        group["name"] = (motion_group_name.value or "").strip()
        frames = group["frames"]
        if not frames:
            return

        f_idx = selected_frame_idx["value"]
        if f_idx < 0 or f_idx >= len(frames):
            return

        frames[f_idx] = {
            "m1": _safe_float(frame_m1.value, 0.0),
            "m2": _safe_float(frame_m2.value, 0.0),
            "m3": _safe_float(frame_m3.value, 0.0),
            "m4": _safe_float(frame_m4.value, 0.0),
            "duration_ms": max(0, min(65535, _safe_int(frame_dur.value, 500))),
            "action": max(0, min(255, _safe_int(frame_act.value, 0))),
        }

    def on_motion_group_change(e):
        if suspend_editor_events["value"]:
            return
        _commit_motion_editor_to_model()
        selected_group_idx["value"] = max(0, min(TOTAL_ACTION_GROUPS - 1, _safe_int(motion_group_dd.value, 0)))
        selected_frame_idx["value"] = 0
        _refresh_motion_editor()
        refresh_motion_view()
        page.update()

    def on_motion_frame_change(e):
        if suspend_editor_events["value"]:
            return
        _commit_motion_editor_to_model()
        selected_frame_idx["value"] = max(0, _safe_int(motion_frame_dd.value, 0))
        _refresh_motion_editor()
        refresh_motion_view()
        page.update()

    def on_add_motion_frame(e):
        _commit_motion_editor_to_model()
        group = motion_model["groups"][selected_group_idx["value"]]
        if len(group["frames"]) >= MAX_FRAMES:
            toast(f"单组最多 {MAX_FRAMES} 帧", "#E65100")
            return
        group["frames"].append({
            "m1": 0.0, "m2": 0.0, "m3": 0.0, "m4": 0.0,
            "duration_ms": 500, "action": 0,
        })
        selected_frame_idx["value"] = len(group["frames"]) - 1
        _refresh_motion_editor()
        refresh_motion_view()
        page.update()

    def on_del_motion_frame(e):
        _commit_motion_editor_to_model()
        group = motion_model["groups"][selected_group_idx["value"]]
        frames = group["frames"]
        if not frames:
            return
        del frames[selected_frame_idx["value"]]
        if selected_frame_idx["value"] >= len(frames):
            selected_frame_idx["value"] = max(0, len(frames) - 1)
        _refresh_motion_editor()
        refresh_motion_view()
        page.update()

    motion_group_dd.on_change = on_motion_group_change
    motion_frame_dd.on_change = on_motion_frame_change
    motion_add_frame_btn.on_click = on_add_motion_frame
    motion_del_frame_btn.on_click = on_del_motion_frame

    # ---- 日志：设备日志与通信日志分离 ----
    def ui_log(msg, color=None):
        ts = time.strftime("%H:%M:%S")
        log_text.value += f"[{ts}] {msg}\n"
        # 限制最大行数，避免过大
        lines = log_text.value.splitlines()
        if len(lines) > 1000:
            log_text.value = "\n".join(lines[-1000:])
        page.update()

    def ui_conn_log(msg, color=None):
        ts = time.strftime("%H:%M:%S")
        conn_log_text.value += f"[{ts}] {msg}\n"
        # 限制最大行数，避免过大
        lines = conn_log_text.value.splitlines()
        if len(lines) > 1000:
            conn_log_text.value = "\n".join(lines[-1000:])
        page.update()

    # ---- 发送超时检测 ----
    pending_cmd = {"name": None, "time": 0}  # 等待响应的命令

    def _start_timeout(cmd_name, timeout=5):
        """发送后启动超时检测"""
        pending_cmd["name"] = cmd_name
        pending_cmd["time"] = time.time()
        def _check():
            time.sleep(timeout)
            if pending_cmd["name"] == cmd_name and time.time() - pending_cmd["time"] >= timeout - 0.1:
                pending_cmd["name"] = None
                ui_conn_log(f"⚠ {cmd_name} 无响应 (超时 {timeout}s)", color="#FFB74D")
                toast(f"{cmd_name} 无响应，请检查连接", "#E65100")
        page.run_thread(_check)

    def _clear_pending():
        """收到有效响应时取消超时"""
        pending_cmd["name"] = None

    # ---- 通用发送 ----
    def send_cmd(cmd_dict):
        global is_connected
        if not is_connected:
            ui_conn_log("⚠ 未连接，请先连接设备", color="#FFB74D")
            toast("未连接，请先连接设备", "#E65100")
            return
        try:
            payload = json.dumps(cmd_dict, separators=(',', ':')) + "\r\n"
            tcp_client.send(payload.encode("utf-8"))
            ui_conn_log(f"Tx {payload.strip()}", color=ACCENT)
            # 运动配置数据量大（~6KB hex），给更长超时
            cmd_name = cmd_dict.get("cmd", "unknown")
            timeout = 15 if cmd_name in ("read_motion_cfg", "write_motion_cfg", "read_log") else 5
            _start_timeout(cmd_name, timeout)
        except Exception as ex:
            ui_conn_log(f"✖ Tx 失败: {ex}", color="#EF5350")
            toast(f"发送失败: {ex}", "#C62828")

    # ---- 从缓冲区提取完整 JSON 对象（花括号配对，不依赖 \n 分隔） ----
    def _extract_json(buf):
        """从 buf 中提取第一个完整的 {...} JSON 对象。
        返回 (json_str, remaining_buf)，如果没有完整对象返回 (None, buf)。
        """
        start = buf.find('{')
        if start < 0:
            return None, ""  # 没有 JSON 开头，丢弃
        depth = 0
        in_str = False
        esc = False
        for i in range(start, len(buf)):
            c = buf[i]
            if esc:
                esc = False
                continue
            if c == '\\' and in_str:
                esc = True
                continue
            if c == '"' and not esc:
                in_str = not in_str
                continue
            if in_str:
                continue
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return buf[start:i+1], buf[i+1:]
        return None, buf  # JSON 不完整，保留等更多数据

    # ---- 后台线程：持续接收 TCP 数据 ----
    def receive_data():
        global is_connected
        print("[RX] receive_data thread started")
        buf = ""
        while is_connected:
            try:
                data = tcp_client.recv(8192)
                if data:
                    print(f"[RX] {len(data)}B received")
                    buf += data.decode("utf-8", errors="replace")
                    # 循环提取所有完整的 JSON 对象
                    while True:
                        json_str, buf = _extract_json(buf)
                        if json_str is None:
                            break
                        # W800 在 TCP 传输中每 ~512B 插入 \r\n，去掉后再解析
                        json_str = json_str.replace('\r', '').replace('\n', '')
                        try:
                            obj = json.loads(json_str)
                        except Exception as e:
                            print(f"[RX JSON ERR] {e} | len={len(json_str)}")
                            ui_conn_log(f"Rx JSON解析错误 ({len(json_str)}B)", color="#FFB74D")
                            continue
                        t = obj.get("type")
                        _clear_pending()
                        if t == "pong":
                            ui_conn_log("Rx Pong", color="#81C784")
                            toast("Ping 成功")
                        elif t == "sys_cfg":
                            dev_ip.value = obj.get("ip", dev_ip.value)
                            dev_mask.value = obj.get("netmask", dev_mask.value)
                            dev_gw.value = obj.get("gateway", dev_gw.value)
                            srv_ip.value = obj.get("server_ip", srv_ip.value)
                            srv_port.value = str(obj.get("server_port", srv_port.value))
                            dbg_port.value = str(obj.get("debug_port", dbg_port.value))
                            wifi_ssid.value = obj.get("wifi_ssid", wifi_ssid.value)
                            wifi_psk.value = obj.get("wifi_psk", wifi_psk.value)
                            # 新增参数自动更新
                            angle_min = obj.get("angle_min", None)
                            angle_max = obj.get("angle_max", None)
                            current_limit = obj.get("current_limit", None)
                            grip_force_max = obj.get("grip_force_max", None)
                            if angle_min and isinstance(angle_min, list):
                                for i in range(min(4, len(angle_min))):
                                    angle_min_fields[i].value = str(angle_min[i])
                            if angle_max and isinstance(angle_max, list):
                                for i in range(min(4, len(angle_max))):
                                    angle_max_fields[i].value = str(angle_max[i])
                            if current_limit and isinstance(current_limit, list):
                                for i in range(min(4, len(current_limit))):
                                    current_limit_fields[i].value = str(current_limit[i])
                            if grip_force_max is not None:
                                grip_force_max_field.value = str(grip_force_max)
                            page.update()
                            ui_conn_log("Rx sys_cfg", color="#81C784")
                            toast("系统配置已读取")
                        elif t == "ack":
                            ui_conn_log(f"Rx ACK {obj.get('cmd')}", color="#81C784")
                            toast(f"{obj.get('cmd')} 操作成功")
                        elif t == "error":
                            ui_conn_log(f"Rx ERROR {obj.get('cmd')}: {obj.get('msg')}", color="#EF5350")
                            if obj.get('cmd') == "write_motion_cfg" and obj.get('msg') == "unsupported_cmd":
                                motion_write_supported["ok"] = False
                                _refresh_motion_editor()
                                page.update()
                            toast(f"{obj.get('cmd')} 失败: {obj.get('msg')}", "#C62828")
                        elif t == "log_data":
                            _handle_log_page(obj)
                        elif t == "motion_cfg":
                            decode_motion_hex(obj.get("hex", ""))
                            ui_conn_log("Rx motion_cfg", color="#81C784")
                            toast("运动配置已读取")
                        else:
                            brief = json_str[:120] + ('...' if len(json_str) > 120 else '')
                            ui_conn_log(f"Rx: {brief}", color="#81C784")
                    # 防止 buf 无限增长（超过 64KB 且没有有效 JSON 则截断）
                    if len(buf) > 65536:
                        print(f"[RX] buf overflow ({len(buf)}B), truncating")
                        buf = buf[-4096:]
                else:
                    print("[RX] recv returned empty, connection closed")
                    break
            except socket.timeout:
                continue
            except Exception as rx_err:
                print(f"[RX EXCEPTION] {rx_err}")
                break
        is_connected = False
        print("[RX] receive_data thread exiting")
        ui_conn_log("连接已断开", color="#EF5350")
        toast("连接已断开", "#C62828")
        update_status()

    # ---- 连接 / 断开 ----
    def connect_click(e):
        global is_connected, tcp_client
        if is_connected:
            try:
                tcp_client.close()
            except Exception:
                pass
            is_connected = False
            ui_conn_log("已手动断开连接", color="#FFB74D")
            toast("已断开连接", "#E65100")
            update_status()
            return

        def _auto_read_configs_after_connect():
            send_cmd({"cmd": "read_sys_cfg"})
            for _ in range(50):
                if pending_cmd["name"] is None:
                    break
                time.sleep(0.1)
            send_cmd({"cmd": "read_motion_cfg"})

        def do_connect():
            global tcp_client, is_connected
            ui_conn_log("正在连接...", color=ACCENT)
            try:
                auto_ip = detect_hotspot_gateway_ip()
                target_ip = ip_input.value.strip()
                if auto_ip:
                    target_ip = auto_ip
                    ip_input.value = auto_ip
                    ui_conn_log(f"自动识别热点网关IP: {auto_ip}", color="#81C784")
                else:
                    ui_conn_log("未识别到热点网关IP，使用手动输入IP", color="#FFB74D")
                page.update()

                c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                c.settimeout(5)
                c.connect((target_ip, int(port_input.value.strip())))
                c.settimeout(1)
                tcp_client = c
                is_connected = True
                ui_conn_log("✔ 连接成功!", color="#66BB6A")
                toast("连接成功!")
                update_status()
                page.run_thread(receive_data)
                page.run_thread(_auto_read_configs_after_connect)
            except Exception as ex:
                ui_conn_log(f"✖ 连接失败: {ex}", color="#EF5350")
                toast(f"连接失败: {ex}", "#C62828")
                is_connected = False
                update_status()

        page.run_thread(do_connect)

    # ---- 命令按钮事件 ----
    def ping_click(e):
        send_cmd({"cmd": "ping"})

    def read_sys_click(e):
        send_cmd({"cmd": "read_sys_cfg"})

    def save_sys_click(e):
        send_cmd({
            "cmd": "write_sys_cfg",
            "ip": dev_ip.value.strip(),
            "netmask": dev_mask.value.strip(),
            "gateway": dev_gw.value.strip(),
            "server_ip": srv_ip.value.strip(),
            "server_port": int(srv_port.value or 0),
            "debug_port": int(dbg_port.value or 0),
            "wifi_ssid": wifi_ssid.value.strip(),
            "wifi_psk": wifi_psk.value.strip(),
            "angle_min": [float(f.value or 0) for f in angle_min_fields],
            "angle_max": [float(f.value or 0) for f in angle_max_fields],
            "current_limit": [float(f.value or 0) for f in current_limit_fields],
            "grip_force_max": float(grip_force_max_field.value or 0),
        })

    def read_motion_click(e):
        send_cmd({"cmd": "read_motion_cfg"})

    def save_motion_click(e):
        if not motion_write_supported["ok"]:
            toast("当前固件不支持 write_motion_cfg", "#E65100")
            return
        _commit_motion_editor_to_model()
        try:
            hex_data = _motion_model_to_hex(motion_model)
        except Exception as ex:
            toast(f"运动配置编码失败: {ex}", "#C62828")
            return
        send_cmd({"cmd": "write_motion_cfg", "hex": hex_data})

    motion_save_btn.on_click = save_motion_click

    def _decode_log_hex(hex_str):
        """将 hex 解码为可读字符串，忽略 0xFF（Flash 擦除状态）和 0x00"""
        try:
            raw = bytes.fromhex(hex_str)
        except Exception:
            return hex_str
        # 过滤掉 0xFF 和 0x00，保留可打印字符
        chars = []
        for b in raw:
            if b == 0xFF or b == 0x00:
                continue
            if 0x20 <= b <= 0x7E:  # ASCII 可打印
                chars.append(chr(b))
            elif b == 0x0A:  # \n
                chars.append('\n')
            elif b == 0x0D:  # \r
                continue
            else:
                chars.append(f'\\x{b:02X}')
        return ''.join(chars)

    def _is_all_ff(hex_str):
        """检查 hex 字符串是否全为 FF"""
        return len(hex_str) > 0 and all(c in 'fF' for c in hex_str)

    def _request_next_log_page():
        """请求下一页日志"""
        st = _log_reading
        if not st["active"]:
            return
        offset = st["page"] * LOG_PAGE_SIZE
        send_cmd({"cmd": "read_log", "offset": offset, "length": LOG_PAGE_SIZE})

    def _handle_log_page(obj):
        """处理收到的一页日志数据"""
        st = _log_reading
        hex_data = obj.get('hex', '')
        offset = obj.get('offset', 0)
        length = obj.get('len', 0)

        ui_conn_log(f"Rx log_data page={st['page']} off={offset} len={length}", color="#81C784")

        # 检查是否全 FF
        if log_auto_stop.value and _is_all_ff(hex_data):
            st["active"] = False
            # 显示累积结果
            _flush_log_buf()
            ui_conn_log(f"日志读取完成（第{st['page']}页遇到空白区域）", color="#81C784")
            toast(f"日志读取完成 ({st['page']}页)")
            return

        # 累积数据
        try:
            st["buf"] += bytes.fromhex(hex_data)
        except Exception:
            pass

        st["page"] += 1
        if st["page"] >= st["max_pages"]:
            st["active"] = False
            _flush_log_buf()
            ui_conn_log(f"日志读取完成（已读 {st['page']} 页）", color="#81C784")
            toast(f"日志读取完成 ({st['page']}页)")
            return

        # 请求下一页
        _request_next_log_page()

    def _flush_log_buf():
        """将累积的日志数据解码并显示"""
        st = _log_reading
        raw = st["buf"]
        if not raw:
            log_text.value = "(日志为空)"
            page.update()
            return
        # 截断到第一个连续 0xFF 区域
        end = len(raw)
        for i in range(len(raw)):
            if raw[i] == 0xFF:
                if all(b == 0xFF for b in raw[i:min(i+8, len(raw))]):
                    end = i
                    break
        raw = raw[:end]
        if not raw:
            log_text.value = "(日志为空)"
            page.update()
            return
        # 解码并显示
        text = raw.decode('utf-8', errors='replace')
        log_text.value = text + f"\n--- 共 {len(raw)} 字节 ---"
        page.update()

    def read_log_click(e):
        try:
            max_p = int(log_pages_input.value or 4)
            if max_p < 1: max_p = 1
            if max_p > 128: max_p = 128  # 最多 32KB
        except Exception:
            max_p = 4
        _log_reading["active"] = True
        _log_reading["buf"] = b""
        _log_reading["page"] = 0
        _log_reading["max_pages"] = max_p
        log_text.value = ""
        ui_log(f"正在读取日志 (最多 {max_p} 页 = {max_p * LOG_PAGE_SIZE}B)...", color=ACCENT)
        _request_next_log_page()

    def clear_log_click(e):
        send_cmd({"cmd": "clear_log"})
        log_text.value = ""
        page.update()

    # ---- 按钮组件 ----
    connect_btn = ft.Button(
        "连接设备", icon=ft.Icons.WIFI,
        on_click=connect_click,
        bgcolor=ACCENT, color="white",
        style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=8)),
        height=42, expand=True,
    )

    def _cmd_button(text, icon, on_click, bgcolor=BG_CARD):
        return ft.Button(
            text, icon=icon, on_click=on_click,
            bgcolor=bgcolor, color="white",
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=8),
                side=ft.BorderSide(1, ACCENT_DIM),
            ),
            height=44, expand=True,
        )

    motion_add_frame_btn.height = 40
    motion_add_frame_btn.bgcolor = BG_CARD
    motion_add_frame_btn.color = "white"
    motion_add_frame_btn.style = ft.ButtonStyle(
        shape=ft.RoundedRectangleBorder(radius=8),
        side=ft.BorderSide(1, ACCENT_DIM),
    )
    motion_del_frame_btn.height = 40
    motion_del_frame_btn.bgcolor = BG_CARD
    motion_del_frame_btn.color = "white"
    motion_del_frame_btn.style = ft.ButtonStyle(
        shape=ft.RoundedRectangleBorder(radius=8),
        side=ft.BorderSide(1, ACCENT_DIM),
    )
    motion_save_btn.height = 44
    motion_save_btn.expand = True
    motion_save_btn.bgcolor = "#2E7D32"
    motion_save_btn.color = "white"
    motion_save_btn.style = ft.ButtonStyle(
        shape=ft.RoundedRectangleBorder(radius=8),
        side=ft.BorderSide(1, ACCENT_DIM),
    )

    # ---- 布局 ----
    header = ft.Container(
        content=ft.Row([
            ft.Icon(ft.Icons.PRECISION_MANUFACTURING, color=ACCENT, size=28),
            ft.Column([
                ft.Text("机械臂 调试控制台", size=18, weight=ft.FontWeight.BOLD, color="white"),
                ft.Text("W800 WiFi Debug Tool", size=11, color=TEXT_DIM),
            ], spacing=0),
            ft.Container(expand=True),
            ft.Row([status_icon, status_text], spacing=4),
        ], alignment=ft.MainAxisAlignment.START),
        padding=ft.Padding(bottom=10),
    )

    conn_controls_mobile = [
        ft.Text("网络连接", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([ip_input], spacing=10),
        ft.Row([port_input], spacing=10),
        ft.Row([connect_btn], spacing=10),
        ft.Row([_cmd_button("Ping", ft.Icons.PODCASTS, ping_click, bgcolor="#263238")], spacing=10),
    ]

    conn_controls_desktop = [
        ft.Text("网络连接", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([ip_input, port_input], spacing=10),
        ft.Row([connect_btn, _cmd_button("Ping", ft.Icons.PODCASTS, ping_click, bgcolor="#263238")], spacing=10),
    ]

    conn_card = ft.Container(
        content=ft.Column(conn_controls_mobile if is_mobile else conn_controls_desktop, spacing=10),
        bgcolor=BG_CARD, border_radius=12, padding=16,
        border=ft.Border.all(1, "#2A2A3E"),
    )

    def clear_conn_log_click(e):
        conn_log_text.value = ""
        page.update()

    conn_log_card = ft.Container(
        content=ft.Column([
            ft.Row([
                ft.Icon(ft.Icons.SYNC_ALT, color=ACCENT, size=16),
                ft.Text("通信日志", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
                ft.Container(expand=True),
                ft.TextButton("清空", icon=ft.Icons.DELETE_SWEEP,
                              on_click=clear_conn_log_click,
                              style=ft.ButtonStyle(color=TEXT_DIM)),
            ], spacing=6),
            ft.Container(content=conn_log_text, expand=True, bgcolor=BG_LOG, border_radius=8, padding=10),
        ], spacing=6, expand=True),
        bgcolor=BG_CARD, border_radius=12, padding=16, expand=True,
        border=ft.Border.all(1, "#2A2A3E"),
    )

    angle_min_fields = [ft.TextField(label=f"角度下限{i+1}", value="-90.0", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur) for i in range(4)]
    angle_max_fields = [ft.TextField(label=f"角度上限{i+1}", value="90.0", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur) for i in range(4)]
    current_limit_fields = [ft.TextField(label=f"电流限制{i+1}", value="2.0", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur) for i in range(4)]
    grip_force_max_field = ft.TextField(label="最大抓取力度", value="5.0", border_color=ACCENT_DIM, focused_border_color=ACCENT, text_size=13, expand=True, on_submit=blur)

    sys_controls_mobile = [
        ft.Text("系统配置", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([dev_ip], spacing=10),
        ft.Row([dev_mask], spacing=10),
        ft.Row([dev_gw], spacing=10),
        ft.Row([srv_ip], spacing=10),
        ft.Row([srv_port], spacing=10),
        ft.Row([dbg_port], spacing=10),
        ft.Row([wifi_ssid], spacing=10),
        ft.Row([wifi_psk], spacing=10),
        ft.Divider(height=1, color="#333"),
        ft.Text("角度限制", size=12, color=ACCENT, weight=ft.FontWeight.W_600),
        ft.Row(angle_min_fields[:2], spacing=10),
        ft.Row(angle_min_fields[2:], spacing=10),
        ft.Row(angle_max_fields[:2], spacing=10),
        ft.Row(angle_max_fields[2:], spacing=10),
        ft.Divider(height=1, color="#333"),
        ft.Text("电流与力度", size=12, color=ACCENT, weight=ft.FontWeight.W_600),
        ft.Row(current_limit_fields[:2], spacing=10),
        ft.Row(current_limit_fields[2:], spacing=10),
        ft.Row([grip_force_max_field], spacing=10),
        ft.Row([_cmd_button("读取配置", ft.Icons.CLOUD_DOWNLOAD, read_sys_click)], spacing=10),
        ft.Row([_cmd_button("保存配置", ft.Icons.SAVE, save_sys_click, bgcolor="#2E7D32")], spacing=10),
    ]

    sys_controls_desktop = [
        ft.Text("系统配置", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([dev_ip, dev_mask], spacing=10),
        ft.Row([dev_gw, srv_ip], spacing=10),
        ft.Row([srv_port, dbg_port, wifi_ssid], spacing=10),
        ft.Row([wifi_psk], spacing=10),
        ft.Divider(height=1, color="#333"),
        ft.Text("角度限制", size=12, color=ACCENT, weight=ft.FontWeight.W_600),
        ft.Row(angle_min_fields, spacing=10),
        ft.Row(angle_max_fields, spacing=10),
        ft.Divider(height=1, color="#333"),
        ft.Text("电流与力度", size=12, color=ACCENT, weight=ft.FontWeight.W_600),
        ft.Row(current_limit_fields, spacing=10),
        ft.Row([grip_force_max_field], spacing=10),
        ft.Row([
            _cmd_button("读取配置", ft.Icons.CLOUD_DOWNLOAD, read_sys_click),
            _cmd_button("保存配置", ft.Icons.SAVE, save_sys_click, bgcolor="#2E7D32"),
        ], spacing=10),
    ]

    sys_card = ft.Container(
        content=ft.Column(sys_controls_mobile if is_mobile else sys_controls_desktop, spacing=10, scroll="auto", expand=True),
        bgcolor=BG_CARD, border_radius=12, padding=16,
        border=ft.Border.all(1, "#2A2A3E"),
        expand=True
    )

    motion_controls_mobile = [
        ft.Text("运动配置", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([_cmd_button("读取运动配置", ft.Icons.SETTINGS_INPUT_COMPOSITE, read_motion_click)], spacing=10),
        ft.Row([motion_save_btn], spacing=10),
        ft.Row([motion_group_dd, motion_frame_dd], spacing=10),
        ft.Row([motion_group_name], spacing=10),
        ft.Row([frame_m1, frame_m2], spacing=10),
        ft.Row([frame_m3, frame_m4], spacing=10),
        ft.Row([frame_dur, frame_act], spacing=10),
        ft.Row([motion_add_frame_btn, motion_del_frame_btn], spacing=10),
        ft.Container(
            content=motion_data_view, expand=True,
            bgcolor=BG_LOG, border_radius=8, padding=10,
        ),
    ]

    motion_controls_desktop = [
        ft.Text("运动配置", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
        ft.Row([
            _cmd_button("读取运动配置", ft.Icons.SETTINGS_INPUT_COMPOSITE, read_motion_click),
            motion_save_btn,
        ], spacing=10),
        ft.Row([motion_group_dd, motion_frame_dd, motion_group_name], spacing=10),
        ft.Row([frame_m1, frame_m2, frame_m3, frame_m4], spacing=10),
        ft.Row([frame_dur, frame_act, motion_add_frame_btn, motion_del_frame_btn], spacing=10, wrap=True),
        ft.Container(
            content=motion_data_view, expand=True,
            bgcolor=BG_LOG, border_radius=8, padding=10,
        ),
    ]

    motion_card = ft.Container(
        content=ft.Column(motion_controls_mobile if is_mobile else motion_controls_desktop, spacing=10, expand=True),
        bgcolor=BG_CARD, border_radius=12, padding=16, expand=True,
        border=ft.Border.all(1, "#2A2A3E"),
    )

    log_card = ft.Container(
        content=ft.Column([
            ft.Row([
                ft.Icon(ft.Icons.TERMINAL, color=ACCENT, size=16),
                ft.Text("日志", size=13, weight=ft.FontWeight.W_600, color=TEXT_DIM),
                ft.Container(expand=True),
                ft.TextButton("清空本地", icon=ft.Icons.DELETE_SWEEP,
                              on_click=clear_log_click,
                              style=ft.ButtonStyle(color=TEXT_DIM)),
            ], spacing=6),
            ft.Row([log_pages_input, log_auto_stop], spacing=10, wrap=True),
            ft.Row([
                _cmd_button("读取日志", ft.Icons.ARTICLE, read_log_click),
                _cmd_button("清空设备日志", ft.Icons.DELETE, clear_log_click, bgcolor="#B71C1C"),
            ], spacing=10),
            ft.Container(content=log_text, expand=True, bgcolor=BG_LOG, border_radius=8, padding=10),
        ], spacing=8, expand=True),
        bgcolor=BG_CARD, border_radius=12, padding=16, expand=True,
        border=ft.Border.all(1, "#2A2A3E"),
    )

    # ---- 简单分页（兼容旧版 Flet，无 Tabs 构造差异风险） ----
    content_holder = ft.Container(content=ft.Column([conn_card, ft.Container(height=8), conn_log_card], expand=True, spacing=8), expand=True)

    def switch_tab(tab_key):
        if tab_key == "连接":
            content_holder.content = ft.Column([conn_card, ft.Container(height=8), conn_log_card], expand=True, spacing=8)
        elif tab_key == "系统配置":
            content_holder.content = sys_card
        elif tab_key == "运动配置":
            content_holder.content = motion_card
        else:
            content_holder.content = log_card
        page.update()

    tab_bar = ft.Row(
        [
            ft.Button("连接", on_click=lambda e: switch_tab("连接"), expand=True),
            ft.Button("系统配置", on_click=lambda e: switch_tab("系统配置"), expand=True),
            ft.Button("运动配置", on_click=lambda e: switch_tab("运动配置"), expand=True),
            ft.Button("日志", on_click=lambda e: switch_tab("日志"), expand=True),
        ],
        spacing=6,
        wrap=False,
    )

    # 主内容列
    main_col = ft.Column(
        [header, tab_bar, ft.Container(height=6), content_holder],
        expand=True, spacing=0,
    )
    # 浮动反馈层（底部居中，不影响布局）
    feedback_float = ft.Container(
        content=ft.Row([feedback_bar], alignment=ft.MainAxisAlignment.CENTER),
        bottom=20, left=0, right=0,
    )

    refresh_motion_view()
    _refresh_motion_editor()
    page.add(ft.Stack([main_col, feedback_float], expand=True))


if __name__ == "__main__":
    ft.run(main)