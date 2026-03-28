# SurgiDeliver — Intelligent Surgical Instrument Delivery Arm（servo_bus 版本）

本仓库为 Renesas RA6M5 + FreeRTOS 的嵌入式固件工程，包含语音识别、屏幕 UI、网络状态上报与机器人运控（示教/回放/空闲保持）、夹爪控制以及触觉/力反馈处理。

重要：本 README 以 **servo_bus 版本**为准。当前实际生效的运控主链为 **`servo_bus -> motion_ctrl -> drv_servo`**；旧的 `can_comms/CANFD` 逻辑为历史遗留路径，可能仍保留在代码中供兼容或参考，但不作为当前主控制链路。

---

## 目录

- [系统架构](#系统架构)
- [软件任务](#软件任务)
- [运控主链](#运控主链)
- [仓库结构](#仓库结构)
- [桌面监控工具](#桌面监控工具)
- [构建与烧录](#构建与烧录)

---

## 系统架构

系统按功能大致分为三层：

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1 · Edge Intelligence                                │
│  Mic → Edge AI Inference → Vote & Command                   │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Layer 2 · Motion Execution (servo_bus)                      │
│  Trajectory → Motion State Machine → Serial Servo Bus        │
│  + Gripper Control + Touch / Force Feedback                  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Layer 3 · Monitoring                                         │
│  LVGL UI + Ethernet UDP Reporting + PC Tool                   │
└─────────────────────────────────────────────────────────────┘
```

架构图（Mermaid 源码）在 `docs/architecture/` 目录，可导入 draw.io（Arrange → Insert → Advanced → Mermaid）。

---

## 软件任务

固件运行于 FreeRTOS，主要任务如下（入口文件在 `src/*_entry.c`）：

| Task | Entry file | 说明 |
|------|-----------|------|
| `servo_bus` | `src/servo_bus_entry.c` | 实际运控主线程：模式切换/周期调度/链路与急停 |
| `screen_interact` | `src/screen_interact_entry.c` | LVGL UI 与示教保存 |
| `voice_command` | `src/voice_command_entry.c` | 麦克风采集 + Edge-AI 推理 + 器械队列 |
| `net_connect` | `src/net_connect_entry.c` | 以太网就绪后 UDP 状态上报 |
| `wifi_debug` | `src/wifi_debug_entry.c` | WiFi AT 调试与配置 |
| `log_task` | `src/log_task_entry.c` | 日志输出 |
| `can_comms` | `src/can_comms_entry.c` | 历史遗留（当前不作为运控主链） |

---

## 运控主链

当前实际生效链路：

- **任务层**：`src/servo_bus_entry.c`
- **状态机层**：`src/modules/motion_ctrl/`（示教/回放/空闲保持、夹爪与交接）
- **轨迹层**：`src/modules/motion_ctrl/trajectory.c`
- **驱动层**：`src/modules/servo/drv_servo.c`（串口舵机）
- **触觉层**：`src/modules/touch/drv_touch.c` + `motion_ctrl` 内滤波/零偏/判定

---

## 仓库结构

```
surgideliver/
├── src/                        # 业务与驱动源码（主要维护范围）
│   ├── modules/
│   │   ├── motion_ctrl/        # 运控状态机 + 轨迹
│   │   ├── servo/              # 串口舵机驱动
│   │   ├── touch/              # 触觉传感驱动
│   │   ├── screen/             # 显示/触摸/LVGL UI（含第三方 LVGL）
│   │   ├── wifi/               # WiFi AT + NVM
│   │   ├── ethernet/           # 网络钩子
│   │   ├── log/                # 日志子系统
│   │   └── canfd/              # 历史遗留：CAN-FD 相关（当前非主链）
│   ├── tools/                  # shared_data / packet_packer 等
│   ├── edge-ai/                # Edge Impulse 模型集成
│   └── *_entry.c               # FreeRTOS 任务入口
├── app/                        # 桌面监控工具
├── docs/                       # 文档/计划/架构图
├── reference/                  # 参考资料
├── test/                       # 测试脚本/数据/记录
├── ra/                         # FSP HAL（自动生成）
└── ra_cfg/                     # FSP 配置（自动生成）
```

---

## 桌面监控工具

`app/main.py` 为 Python 桌面工具（Flet），用于连接设备并查看状态/配置与调试。

---

## 构建与烧录

本工程使用 Renesas FSP + e² studio 开发：

1. 用 e² studio 打开工程
2. 选择目标构建配置
3. Build（Project → Build）
4. 通过 J-Link 下载并调试（Run → Debug）
