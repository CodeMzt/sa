# 电机数据模拟工具

用于模拟单片机发送电机状态数据包到 PC，以便进行以太网通信测试。

## 文件说明

| 文件 | 说明 |
|------|------|
| `mock_motor_sender.py` | 模拟发送端：循环发送5个电机的状态数据包 |
| `mock_motor_receiver.py` | 接收端：接收并解析验证数据包格式 |
| `udp_reliability_receiver.py` | 可靠性测试接收端：统计丢包率、延迟、抖动 |

## 协议规格

**数据包格式 (motor_packet_t)：**
- 总大小：44 字节
- 字节序：小端序（Intel x86 标准）
- 发送周期：50ms（20Hz）

```
偏移  字段              大小     说明
────────────────────────────────────
 0    header[0]        1 byte   0xA5
 1    header[1]        1 byte   0x5A
 2-21 angles[5]        20 bytes 5个浮点数（4关节 + 1夹爪）
22-41 torques[5]       20 bytes 5个浮点数（4关节 + 1夹爪）
 42   checksum         1 byte   字节2~41的累加和（mod 256）
 43   tail             1 byte   0xED
```

**网络配置：**
- 单片机 IP：`192.168.31.100`
- PC IP：`192.168.31.250`
- 协议：UDP
- 端口：`2333`

## 使用方法

### 0. UDP 可靠性统计（推荐用于实测）

该脚本按“首包对齐 + 固定发送周期”方法统计指标，不改变 MCU 原有 44B 协议。

```bash
python script/udp_reliability_receiver.py --listen-ip 0.0.0.0 --port 2333 --interval-ms 50 --duration-s 1800 --csv script/udp_test_result.csv
```

输出指标包括：
- 接收包数 / 预期包数 / 估计丢包率
- 延迟（min/avg/p50/p95/max）
- 抖动（avg/p50/p95/max）

参数说明：
- `--interval-ms`：与 MCU 发送周期一致（当前默认 50ms）
- `--duration-s`：测试时长，默认 1800 秒（30 分钟），0 表示直到手动停止
- `--csv`：导出 CSV（包含整体统计分析 + 逐包统计明细，用于后续论文/报告）；若目录不存在会自动创建，编码为 UTF-8 BOM（utf-8-sig）以兼容 Windows 表格软件

### 1. 本地测试（单机模拟完整链路）

**终端1 - 启动接收端：**
```bash
python script/mock_motor_receiver.py
```

输出示例：
```
Mock Motor Receiver
  Listen: 0.0.0.0:2333 (UDP)
  Expected Motor Count: 5
  Expected Packet Size: 44 bytes

Listening on 0.0.0.0:2333...

[000000] from ('127.0.0.1', 54321) | Angles: M1=-0.000 M2=-0.588 M3=-0.951 M4=-0.951 M5=-0.588  | Torques: T1= 0.500 T2= 0.566 T3= 0.600 T4= 0.566 T5= 0.400
[000010] from ('127.0.0.1', 54321) | Angles: M1= 0.095 M2=-0.475 M3=-0.809 M4=-0.809 M5=-0.475  | Torques: T1= 0.534 T2= 0.581 T3= 0.599 T4= 0.564 T5= 0.404
...
```

**终端2 - 启动发送端：**
```bash
python3 script/mock_motor_sender.py
```

输出示例：
```
Mock Motor Sender
  Target: 192.168.31.250:2333 (UDP)
  Send Interval: 50 ms (20 Hz)
  Motor Count: 5
  Packet Size: 44 bytes

[000000] t= 0.00s | Angles: M1=0.000 M2=-0.588 M3=-0.951 M4=-0.951 M5=-0.588
[000010] t= 0.50s | Angles: M1=0.095 M2=-0.475 M3=-0.809 M4=-0.809 M5=-0.475
...
```

### 2. 网络测试（实际硬件环境）

**假设网络环境：**
- 单片机连接到网络，IP 为 `192.168.31.100`
- PC 连接到同一网络，IP 为 `192.168.31.250`

**PC 端启动接收：**
```bash
# 确认 PC 的实际 IP 地址
ipconfig  # Windows
# 或
ifconfig  # Linux/Mac

# 启动接收端（会监听 0.0.0.0:2333，接受任意源）
python3 script/mock_motor_receiver.py
```

**单片机/发送端配置：**
- 确保 `mock_motor_sender.py` 中的 `TARGET_IP` 设置为 PC 的实际 IP

## 电机动作设计

默认动作为 **正弦波循环运动**：

- **频率**：0.5 Hz（周期 2 秒）
- **幅度**：±1.0 rad（约 ±57°）
- **相位差**：5个电机均匀分布，相邻相差 72°
- **力矩**：基准值 0.5 Nm，叠加 ±0.2 Nm 的调制

这样可以模拟机械臂的平滑关节运动。

## 自定义修改

### 修改发送频率
编辑 `mock_motor_sender.py` 第 24 行：
```python
SEND_INTERVAL = 0.05  # 改为需要的秒数（单位：秒）
```

### 修改目标地址
编辑 `mock_motor_sender.py` 第 20-21 行：
```python
TARGET_IP = "192.168.31.250"   # 改为实际 PC IP
TARGET_PORT = 2333             # 改为实际端口（需与接收端一致）
```

### 修改动作形式
编辑 `mock_motor_sender.py` 中的 `generate_motor_states()` 函数：
```python
def generate_motor_states(t):
    # 修改此处的角度计算逻辑
    angle = AMPLITUDE * math.sin(2 * math.pi * FREQUENCY * t + phase)
    # 例如改为三角波或阶跃波
```

## 故障排查

| 问题 | 原因 | 解决方案 |
|------|------|--------|
| 接收端无法收到数据 | 防火墙阻止 | 允许 Python 或端口 2333 通过防火墙 |
| 接收端显示 ERROR | 包格式不匹配 | 检查 struct.pack 的字节序和大小 |
| 发送频率不稳定 | 系统负载过高 | 优化 Python 脚本或关闭其他程序 |
| 网络连接不上 | IP/网络配置错误 | 验证网络连接和 IP 地址 |

## 依赖

- Python 3.6+
- 标准库：`socket`, `struct`, `math`, `time`

无需额外安装依赖包。

## 许可

用于开发测试。
