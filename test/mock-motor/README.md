# 电机数据模拟工具

用于模拟和验证 MCU 到 PC 的 UDP 电机状态上传链路。

当前协议已经扩展为 6 路电机，上传顺序为：

`J1, J2, J3, J4, J5, GRIPPER`

## 文件说明

| 文件 | 说明 |
| --- | --- |
| `mock_motor_sender.py` | 模拟发送端，按 6 路电机格式持续发送角度和力矩 |
| `mock_motor_receiver.py` | 调试接收端，解析并打印 6 路电机数据 |
| `../udp/udp_reliability_receiver.py` | 可靠性测试接收端，统计延迟、抖动和估算丢包 |

## 协议规格

- 总长度：`52` 字节
- 字节序：小端
- 默认发送周期：`50 ms`

### 数据包格式

| 偏移 | 字段 | 大小 | 说明 |
| --- | --- | --- | --- |
| `0` | `header[0]` | 1 byte | `0xA5` |
| `1` | `header[1]` | 1 byte | `0x5A` |
| `2-25` | `angles[6]` | 24 bytes | 6 路角度，`float` |
| `26-49` | `torques[6]` | 24 bytes | 6 路力矩，`float` |
| `50` | `checksum` | 1 byte | 字节 `2-49` 的累加和低 8 位 |
| `51` | `tail` | 1 byte | `0xED` |

## 使用方法

### 本地联调

先启动接收端：

```bash
python test/mock-motor/mock_motor_receiver.py
```

再启动发送端：

```bash
python test/mock-motor/mock_motor_sender.py
```

### 可靠性测试

```bash
python test/udp/udp_reliability_receiver.py --listen-ip 0.0.0.0 --port 2333 --interval-ms 50 --duration-s 1800 --csv test/udp/udp_test_result.csv
```

## 修改常用项

### 修改目标 IP 和端口

编辑 [mock_motor_sender.py](./mock_motor_sender.py) 中的：

```python
TARGET_IP = "192.168.31.191"
TARGET_PORT = 2333
```

### 修改发送周期

编辑 [mock_motor_sender.py](./mock_motor_sender.py) 中的：

```python
SEND_INTERVAL = 0.05
```

### 修改动作波形

编辑 [mock_motor_sender.py](./mock_motor_sender.py) 中的 `generate_motor_states()`。

## 备注

- Python 模拟脚本和 MCU `packet_packer` 已对齐为同一份 6 路协议。
- MCU 当前上传顺序为 `J1~J5 + GRIPPER`。
