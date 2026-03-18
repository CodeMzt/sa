# SurgiDeliver — Intelligent Surgical Instrument Delivery Arm

An embedded system for an intelligent surgical instrument delivery robotic arm, built on the Renesas RA6M5 microcontroller with FreeRTOS. The system integrates Edge-AI voice recognition, CAN-FD motor control, real-time trajectory planning, and a networked digital-twin monitoring layer.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Hardware Overview](#hardware-overview)
- [Software Overview](#software-overview)
- [Repository Structure](#repository-structure)
- [Desktop Monitoring App](#desktop-monitoring-app)
- [Build & Flash](#build--flash)
- [Author](#author)

---

## System Architecture

The system is organized into three functional layers:

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1 · Edge Intelligence                                │
│  INMP441 Mic → Edge AI Inference → Vote & Command           │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Layer 2 · High-Precision Mechanical Execution              │
│  Cubic-Spline Trajectory + Gravity Compensation             │
│  → Motion-Control State Machine → CAN-FD → EL05 Actuators │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Layer 3 · Digital-Twin Monitoring                          │
│  Local LVGL UI + Ethernet UDP Reporting + PC Tool           │
└─────────────────────────────────────────────────────────────┘
```

Architecture diagrams (Mermaid source) are available in the [`graph/`](graph/) directory and can be imported into draw.io via **Arrange → Insert → Advanced → Mermaid**.

---

## Hardware Overview

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | Renesas RA6M5 | — |
| Joint motors (×4) | EL05 | CAN-FD (CANFD0) |
| Gripper motor (×1) | EL05 | CAN-FD (CANFD0) |
| Ethernet PHY | LAN8720A | RMII |
| WiFi module | W800 | UART (SCI6) |
| Display | ST7796S | SPI1 |
| Touch controller | FT5x06 | I2C (IIC2) |
| Microphone | INMP441 | I2S (SSI0 + GPT) |
| External flash | W25Q64 | QSPI0 |
| Debug UART | — | UART (SCI7) |

---

## Software Overview

The firmware runs on **FreeRTOS** and is divided into the following tasks:

| Task | Entry file | Responsibility |
|------|-----------|----------------|
| `can_comms` | `src/can_comms_entry.c` | CAN-FD communication with EL05 motors |
| `net_connect` | `src/net_connect_entry.c` | Ethernet / UDP state reporting |
| `wifi_debug` | `src/wifi_debug_entry.c` | WiFi AT configuration |
| `screen_interact` | `src/screen_interact_entry.c` | LVGL touch-screen UI |
| `voice_command` | `src/voice_command_entry.c` | Microphone capture + Edge-AI inference |
| `log_task` | `src/log_task_entry.c` | Serial log output |

### Key modules (`src/modules/`)

| Module | Path | Description |
|--------|------|-------------|
| Motion control | `motion_ctrl/` | State machine, cubic-spline trajectory, gravity compensation |
| CAN-FD driver | `canfd/` | RobStride CAN-FD protocol driver |
| Voice / mic driver | `voice/` | INMP441 driver + Edge-AI integration |
| Screen / UI | `screen/` | SPI display driver, I2C touch driver, LVGL port, UI application |
| WiFi driver | `wifi/` | W800 AT driver, NVM configuration storage |
| Ethernet | `ethernet/` | Network hooks for LwIP |
| Logging | `log/` | Lightweight logging subsystem |

### Tools (`src/tools/`)

- **`shared_data`** — inter-task shared state and queues
- **`packet_packer`** — UDP status-packet serialization
- **`test_mode`** — hardware test helpers

---

## Repository Structure

```
surgideliver/
├── src/                        # Firmware source code
│   ├── modules/                # Peripheral and algorithm modules
│   │   ├── canfd/              # CAN-FD driver + RobStride protocol
│   │   ├── ethernet/           # Ethernet / LwIP hooks
│   │   ├── log/                # Logging subsystem
│   │   ├── motion_ctrl/        # Trajectory, gravity comp, state machine
│   │   ├── screen/             # Display, touch, LVGL UI
│   │   ├── voice/              # Microphone driver + Edge-AI
│   │   └── wifi/               # WiFi driver + NVM config
│   ├── tools/                  # Shared utilities
│   ├── edge-ai/                # Edge Impulse model integration
│   ├── *_entry.c               # FreeRTOS task entry points
│   └── hal_warmstart.c         # HAL warm-start hooks
├── app/
│   └── main.py                 # Desktop monitoring & debug console (Flet)
├── graph/                      # Architecture diagrams (Mermaid)
├── reference/                  # Reference datasheets (EL05.pdf)
├── ra/                         # Renesas FSP generated HAL code
├── ra_cfg/                     # FSP configuration files
└── script/                     # Build / utility scripts
```

---

## Desktop Monitoring App

`app/main.py` is a Python desktop application (built with [Flet](https://flet.dev/)) that connects to the device over TCP and provides:

- Real-time system status display
- System configuration read / write
- Motion group & frame configuration editor
- Log page reader

### Requirements

- Python 3.10+
- [Flet](https://flet.dev/) (`pip install flet`)

### Running

```bash
cd app
python main.py
```

Enter the device IP address in the app and click **连接设备** (Connect) to establish a connection.

---

## Build & Flash

The firmware is developed with the **Renesas FSP (Flexible Software Package)** and **e² studio** IDE.

1. Open the project in **e² studio**.
2. Select the target build configuration (`sa Debug` or `surgideliver_arm Debug`).
3. Build the project (**Project → Build**).
4. Flash to the RA6M5 board via the J-Link debug probe (**Run → Debug**).

---

## Author

**Ma Ziteng**
