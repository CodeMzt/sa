- Project stack versions: FSP 6.3.0 (`ra/fsp/inc/fsp_version.h`), LVGL 8.3.11 (`src/modules/screen/lvgl/lvgl.h`).
- Algorithm modules: simplified gravity compensation in `src/modules/motion_ctrl/gravity_comp.c`; natural cubic spline trajectory with Thomas solver in `src/modules/motion_ctrl/trajectory.c`.
- Driver provenance note in source comments: screen/touch drivers mention reference to 100ask (`src/modules/screen/drv_spi_display.c`, `src/modules/screen/drv_i2c_touchpad.c`).
- FSP module usage through generated headers: CANFD (`ra_gen/can_comms.h`), I2C/SPI (`ra_gen/screen_interact.h`), SCI UART (`ra_gen/wifi_debug.h`), QSPI (`ra_gen/hal_data.h`).
- Voice pipeline has an existing test hook: `src/modules/voice/drv_microphone.c` wraps `ei_feed_audio(...)` with `#ifndef TEST`.
- UDP telemetry packet is fixed 44B (`src/tools/packet_packer.h`) with no sequence/timestamp fields yet, limiting direct loss/latency analysis on PC receiver side.
- Current FreeRTOS thread priorities/stacks from `ra_gen`: `voice_command` prio4 stack0x5000, `can_comms` prio3 stack0x800, `screen_interact` prio2 stack0x1000, `wifi_debug` prio2 stack0x8000, `net_connect` prio1 stack0x2000, `log_task` prio1 stack0x800.
- In `src/modules/log/sys_log.c`, `#ifdef DEBUG` path sends logs to UART queue (`g_log_queue`) instead of NVM append; app-side `read_log` reads NVM via WiFi command, so test-log visibility depends on log path choice.
- Added centralized test-thread gating header `src/tools/test_mode.h`: in `VOICE_TEST_MODE` keep `voice_command+log_task`; in `UDP_TEST_MODE` keep `net_connect+log_task`; in `CAN_TEST_MODE` keep `can_comms+log_task`; other entry threads self-delete early via compile-time checks.

- Gear ratio handling for joints 1-4 is centralized in `src/modules/canfd/robstride_motor.c`: outgoing command angle/velocity are multiplied by `g_motor_gear_ratio[]`, and CAN feedback angle/velocity are divided by it; upper layers consume joint-side values from `g_motors[].feedback`.


- e2 studio Debug 构建依赖各目录下自动生成的 `subdir.mk` 显式列出源文件；仅把新 `.c` 文件放进 `src/modules/canfd` 不会自动参与当前 `Debug` 目录构建，除非工程重新生成构建文件。


- NVM manager/类型定义位于 `src/modules/wifi/`（非 `src/modules/storage/`）；`sys_config.zero_offset[4]` 当前无现有业务读写路径，可作为“上次关节角”持久化复用点（需注意与未来“零偏标定”语义潜在冲突）。

- Motion controller uses shared PD gains via `motion_ctrl_config_t.controller.{kp,kd}`; TEACH/PLAYBACK issue motion-control commands while IDLE is no-op (no periodic command output).
- Deep check note (2026-03-17): motion config/sys config getters return raw global pointers (`nvm_get_motion_config`, `nvm_get_sys_config`) while saves memcpy under mutex; control loop reads are lockless, so concurrent WiFi/UI writes can create transient inconsistent reads.
- 2026-03-17 teach refactor baseline: HOME removed standalone TEACH entry; DEBUG MODE is now the only entry and routes to WiFi debug subpage + debug tools (legacy TEACH UI pages).
- 2026-03-18 new app-mcu remote teach command: `teach_jog` in `src/modules/wifi/drv_wifi.c` accepts `motor_id` 1..5 and variable `vel` in range 0.05~0.50 rad/s (both directions, plus 0 stop), parsed as centi-rad/s int16 with firmware abs-range check [5,50]; no ack is sent on success.
- 2026-03-18 jog control behavior: `src/can_comms_entry.c` now runs remote jog in motion-control mode (`robstride_motion_control`) with 300ms watchdog timeout; stale/exit/emergency paths clear state and issue hard stop (`robstride_stop`) on the last jogged motor.

- 2026-03-17 UI cleanup: removed unreachable `PAGE_TEACH_MAIN`/`PAGE_TEACH_FRAMES`, dead nav `event_nav_cb` case 4, and unused `create_teach_page()` wrapper in `src/modules/screen/ui_app.c`; DEBUG entry behavior unchanged.

- 2026-03-18 app runtime dependency: workspace `.venv` may miss `flet`; running `app/main.py` in `.venv` requires installing `flet` first.

- 2026-03-17 WiFi/NVM startup pitfall: generated `g_cfg_save_mutex` is a binary semaphore (`ra_gen/common_data.c`) created empty; first `nvm_save_*` can block if not primed. `src/modules/wifi/nvm_manager.c` now primes/generates lock in `nvm_init()` to avoid deadlock.

- 2026-03-18 startup sync refactor: replaced fixed startup delays in `voice_command`/`can_comms`/`screen_interact` with bounded wait on `g_log_system_ready` (set in `log_task_entry` after `sys_log_init`), and `can_comms` now calls `nvm_init()` explicitly to remove implicit dependency on other threads doing NVM init first.
- FreeRTOS note for this repo: `vTaskDelay()` yields CPU (task enters Blocked); startup-delay issues here are about wasted wall-clock time and fragile implicit dependencies, not scheduler starvation.

- 2026-03-18 jog release 行为结论：改为 motion-control jog + `robstride_stop` 硬停后，IDLE 已改 no-op，不再依赖 `idle_hold_valid` 重捕获机制。

- 2026-03-18 position unwrap重构：关节1~4改为在 `src/modules/canfd/robstride_motor.c::robstride_parse_feedback` 的RX路径即时展开并直接写入 `g_motors[].feedback.position`；`motion_adapter` 不再维护本地展开状态，`src/can_comms_entry.c` 周期调用 `robstride_unwrap_persist_periodic()` 并通过 `zero_offset[4]` 持久化sector，UI监控可直接读取全局位置且无需触发 `motion_adapter_capture_abs` 刷新。
- 2026-03-18 motion-control发送侧约束：`robstride_motion_control` 位置字段最终会被 `ROBSTRIDE_P_MIN/P_MAX` 截断；控制层当前策略为发送前硬边界限制，关节1~4固定夹紧到 ±0.6285rad（约±36°），夹爪按协议窗口限制。
- 2026-03-18 cleanup refactor: duplicated clamp/ID validation in `motion_ctrl` and `can_comms` was centralized into `src/modules/canfd/robstride_motor.{h,c}` via `robstride_clamp_position_cmd` / `robstride_is_motor_id_valid` / `robstride_is_joint_motor_id` / `robstride_get_motor_index`; motion_adapter runtime APIs were removed from business code and merged into motion_ctrl (direct `g_motors` feedback read + `robstride_unwrap_force_persist`). `motion_adapter.{h,c}` now stay as legacy placeholders only to satisfy generated Debug build inputs.

- 2026-03-18 compile pitfall: `g_motors` extern is declared in `src/tools/shared_data.h` (not in `robstride_motor.h`); modules that read global feedback directly must include `shared_data.h`.

- 2026-03-18 RX feedback simplification: `src/modules/canfd/robstride_motor.c::robstride_parse_feedback` now maps position directly as `raw_motor_position / gear_ratio`; unwrap sector state and `robstride_unwrap_*` APIs are removed from `src/**`. `zero_offset[4]` in `src/modules/wifi/nvm_types.h` is retained as legacy reserved field.

- 2026-03-19 HardFault root cause in `src/net_connect_entry.c`: direct call to `g_ether0...linkStatusGet(...)` can crash before PHY control block is initialized/opened (param checking may be disabled). Prefer `FreeRTOS_IsNetworkUp()` in app thread and avoid direct PHY register/status polling here.

- 2026-03-19 CAN test update: in `CAN_TEST_MODE`, `src/can_comms_entry.c` now sends a periodic no-op poll command to JOINT_4 via `robstride_read_param(..., ROBSTRIDE_PARAM_VBUS)` every 100ms, so TX traffic exists even when screen thread is disabled and system is idle.

- 2026-03-19 CAN test pitfall/fix: `CAN_TEST_POLL_DURATION_MS` only limits `can_test_send_noop_poll()`; previous `can_comms_entry` flow still called `arm_motors_init()` (enable + auto-report for IDs 1/2/3/4/gripper), causing continuous non-JOINT4 traffic. In `CAN_TEST_MODE`, `can_comms_entry` now early-branches to JOINT4-only poll loop and skips motion init/playback pipeline.

- 2026-03-19 Logic2 export note: `script/digital.csv` is raw edge stream (`Time [s], Channel 1`) with 1,048,576 rows, not decoded CAN-frame table; use `script/analyze_logic2_digital_can.py` for activity-window/periodicity analysis, or export Logic2 analyzer table if CAN-ID-level analysis is needed.

- 2026-03-19 Logic2 raw CAN analysis utility added: `script/analyze_logic2_digital_can.py` decodes edge-stream CSV (`Time [s], Channel X`) into CAN frames (best-effort classical CAN at inferred 1Mbps), outputs RobStride ID bitfield tables, command/parameter stats, and performance summary; default outputs `script/digital_analysis.md` + `script/digital_analysis.frames.csv`.

- 2026-03-19 Logic2 raw analyzer tuning: `script/analyze_logic2_digital_can.py` now adds SOF candidate filtering params (`--sof-*`), sample-point auto sweep, candidate-noise vs frame-level error split (`error_model` table), and bitrate-selection score no longer collapses to zero when CRC all fail.

- 2026-03-19 raw edge reliability caveat: new `script/digital.csv` still shows heavy sub-bit jitter (<0.35us edges ~77.97%), while burst cadence is stable at 100ms (200/202 intervals), matching `CAN_TEST_POLL_INTERVAL_MS=100` and `CAN_TEST_POLL_DURATION_MS=20000` in `src/can_comms_entry.c`; current CAN_TEST payload is `robstride_stop(JOINT4)`. Therefore edge-decoder CRC/ACK failure here should not be used alone to conclude runtime CAN business failure; prioritize capture point/threshold/probing validation.

- 2026-03-19 decoder enhancement: `script/analyze_logic2_digital_can.py` supports configurable bit synchronization (phase correction with SJW clamp) via `--disable-bit-sync`, `--sync-window`, `--sync-sjw`, `--sync-min-hold`, `--sync-on-any-edge`, plus `--fixed-sample-point` for strict manual timing tests; later a stuffing-path timing bugfix (same day) significantly improved CRC/form outcomes on `script/digital.csv`.


- 2026-03-19 decoder bugfix/tuning: in `script/analyze_logic2_digital_can.py`, stuffing-path timing progression was fixed (stuff bit no longer stalls bit boundary advance). With tuned params (`--bitrate 1000000 --fixed-sample-point --sample-point 0.68 --glitch-us 0.20 --sync-window 0.45 --sync-sjw 0.30 --sync-min-hold 0.15 --sample-vote-count 5 --sample-vote-span 0.08 --frame-sp-retries 3 --frame-sp-step 0.02`), full `script/digital.csv` reached decoded 628, CRC fail 2.40%, ACK missing 19, clean frames 602 (95.86%).
- 2026-03-19 CAN raw analysis artifact cleanup: removed subset/synthetic/tmp intermediate reports in `script/`, keeping full-run outputs (`final_tuned_full*.md/csv` and existing full baseline files). Added measurement-chain error analysis section for “Saleae Logic 2 + Dupont single-ended CANL probing” into both full reports at line ~136.
- 2026-03-19 projection correction: motor count for capacity planning is 4 (not 8), and control commands are considered per-motor at 100Hz when requested. `script/final_tuned_full*.md` projection sections were overwritten accordingly (4-motor model, scenarioB load 14.453%, 70% boundary feedback 964.813Hz/motor).


- 2026-03-19 STOP专项报告口径调整：`script/final_tuned_full.md` 与 `script/final_tuned_full_aggressive.md` 已重写为“仅统计 ID=0x04FD0004(STOP) 的600帧”，移除其它命令统计；分别给出STOP错误帧定位（full:5帧ACK缺失，aggr:1帧ACK缺失）与STOP负载公式结果（约0.145987% @1Mbps）。
