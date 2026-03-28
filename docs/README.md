# Mermaid 图集（draw.io 兼容）

本目录包含项目架构图的 Mermaid 源文件：

- `01_three_layer_architecture.mmd`
- `02_system_hardware_block_diagram.mmd`
- `03_ra6m5_peripheral_modules.mmd`
- `04_software_overall_architecture.mmd`

## draw.io 导入方式

1. 打开 draw.io（diagrams.net）
2. 选择 **Arrange -> Insert -> Advanced -> Mermaid**
3. 粘贴 `.mmd` 文件内容并插入

## 兼容规范

- 使用 `flowchart LR`
- 仅使用基础节点与连线（`[]`、`-->`）
- 不使用 HTML 标签（如 `<br/>`）
- 子图仅单层，避免深层嵌套
- 节点 ID 使用英文数字下划线，显示文本可中文

## 图纸语义口径

- “数字孪生监控”按当前代码实现态表达：实时监控 + 参数下发 + 状态回传
- 网络链路：Ethernet 为主、WiFi 为辅
- 执行器：EL05（4关节 + 夹爪）
- 关键器件型号：LAN8720A、W25Q64
