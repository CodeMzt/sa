# 分离示教模式和回放模式控制频率 Spec

## Why
当前系统中示教模式和回放模式使用相同的控制频率（200Hz），但实际应用中示教模式不需要这么高的频率，为了节省计算资源并满足不同模式的实际需求，需要将两个模式的控制频率分离，将示教模式频率降低到80Hz。

## What Changes
- 修改motion_ctrl.h中的频率定义，增加独立的示教模式频率定义
- 修改motion_ctrl.c中的控制逻辑，根据当前模式使用不同的频率
- 修改can_comms_entry.c中的控制循环，支持动态频率调整
- **BREAKING**: 控制循环的时序逻辑将根据模式变化而调整

## Impact
- 受影响的规格: 运控模式控制频率
- 受影响的代码: 
  - src/modules/motion_ctrl/motion_ctrl.h
  - src/modules/motion_ctrl/motion_ctrl.c
  - src/can_comms_entry.c

## ADDED Requirements
### Requirement: 频率分离控制
系统应能够根据当前运行模式（示教/回放）使用不同的控制频率。

#### Scenario: 示教模式运行
- **WHEN** 系统处于示教模式（MOTION_STATE_TEACHING）
- **THEN** 控制频率应为80Hz（12.5ms周期）

#### Scenario: 回放模式运行
- **WHEN** 系统处于回放模式（MOTION_STATE_PLAYBACK）
- **THEN** 控制频率应为200Hz（5ms周期）

## MODIFIED Requirements
### Requirement: 控制循环执行
原要求：控制循环始终以固定频率（200Hz）执行
新要求：控制循环根据当前模式动态调整执行频率

## REMOVED Requirements
### Requirement: 统一控制频率
**Reason**: 不再需要统一频率，各模式使用独立频率
**Migration**: 通过模式检测自动选择对应频率