# CANFD FIFO中断处理更新规范

## 为什么
当前CANFD配置已更改为FIFO 16帧深度，在4帧时触发中断的模式，但中断处理程序仍使用原有的单帧处理逻辑。需要修改中断处理程序以正确处理FIFO中批量到达的4帧数据。

## 什么变化
- 修改CANFD接收FIFO中断处理程序，使其能够一次处理FIFO中的多帧数据（最多4帧）
- 保持与现有回调函数的兼容性
- 优化中断处理效率，减少中断触发次数

## 影响
- 受影响规格：CANFD驱动模块
- 受影响代码：drv_canfd.c, 中断处理相关函数

## 新增需求
### 需求：批量处理FIFO数据
系统应能够在CANFD接收中断中一次性处理FIFO缓冲区中的多帧数据。

#### 场景：FIFO满4帧触发中断
- **当** FIFO中有4帧数据触发中断时
- **那么** 中断处理程序应循环读取FIFO直到为空，并对每帧数据调用回调函数

## 修改需求
### 需求：CANFD中断处理
修改现有的CANFD中断处理逻辑，从单帧处理改为批量处理。

原实现：
```c
void canfd0_callback(can_callback_args_t *p_args) {
    switch (p_args->event)
    {
        case CAN_EVENT_RX_COMPLETE:
            robstride_parse_feedback(p_args->frame);
            break;
        // ...
    }
}
```

修改后：中断处理程序应能处理多个连续的数据帧。