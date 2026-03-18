# 项目重构与优化计划

该计划旨在对项目进行全面的代码规范化和文档整理，主要涵盖 `src` 目录下的源代码优化（排除 `lvgl`、`edge-ai` 和 `hal_warmstart.c`）以及 `docs` 目录下的文档整理。

## 1. 源代码检查与优化 (src 目录)

本阶段将分模块对源代码进行检查和优化，确保代码风格统一、逻辑清晰、内存高效。

### 1.1 模块 A：入口与基础任务

涉及文件：

* `src\wifi_debug_entry.c`

* `src\log_task_entry.c`

* `src\net_connect_entry.c`

* `src\voice_command_entry.c`

* `src\screen_interact_entry.c`

**优化任务：**

* [ ] **注释标准化**：添加统一的文件头和函数注释，包含 `@author: Ma Ziteng或者 Author: Ma Ziteng`。如果已经有就不用修改

* [ ] **注释清理**：移除冗余说明，保留关键业务逻辑文档。

* [ ] **命名规范化**：将变量、函数、组件命名统一为 `小写 + 下划线` 格式。

* [ ] **标识符简化**：将过于冗长的标识符简化为简洁且有意义的名称。

* [ ] **内存优化**：消除不必要的中间变量。

* [ ] **代码风格紧凑化**：确保所有 `{` 紧跟在 `)` 之后，例如 `if (...) {`。ss同时删除没必要的{}（如果只包裹了一行代码）

* [ ] **无用代码清理**：删除或注释掉未使用的函数、测试代码及过时实现。但是不要删除现有明显有用的测试代码（voice test、can test、udp test）如果不确定某段代码是否有用，请询问人类

* [ ] **日志审查**：在 `LOG_D` 部分仅保留关键调试信息。

### 1.2 模块 B：语音与网络模块

涉及文件：

* `src\modules\voice\drv_microphone.c`

* `src\modules\voice\drv_microphone.h`

* `src\modules\voice\ei_integration.h`

* `src\modules\wifi\drv_wifi.c`

* `src\modules\wifi\drv_wifi.h`

* `src\modules\wifi\nvm_manager.c`

* `src\modules\wifi\nvm_manager.h`

* `src\modules\wifi\nvm_types.h`

* `src\modules\wifi\nvm_config.h`

* `src\modules\ethernet\network_hooks.c`

**优化任务：**

* 同 1.1 节所述的标准化与清理要求。

### 1.3 模块 C：运动控制模块

涉及文件：

* `src\modules\motion_ctrl\motion_ctrl.c`

* `src\modules\motion_ctrl\gravity_comp.c`

* `src\modules\motion_ctrl\trajectory.c`

* `src\modules\canfd\drv_canfd.c`

* `src\modules\canfd\robstride_motor.c`

**优化任务：**

* 同 1.1 节所述的标准化与清理要求。

### 1.4 模块 D：屏幕显示与工具类

涉及文件：

* `src\modules\screen\ui_app.c`

* `src\modules\screen\ui_app.h`

* `src\modules\screen\drv_spi_display.c`

* `src\modules\screen\lv_port.c`

* `src\modules\screen\lv_port.h`

* `src\modules\screen\drv_i2c_touchpad.c`

* `src\tools\shared_data.c`

* `src\tools\shared_data.h`

* `src\tools\packet_packer.c`

* `src\tools\packet_packer.h`

* `src\modules\log\sys_log.c`

* `src\tools\test_mode.h`

**优化任务：**

* 同 1.1 节所述的标准化与清理要求。

***

## 2. 文档整理与规范化 (docs 目录)

本阶段将对现有文档进行去重、整合和格式统一。

### 2.1 文档审查与去重

* [ ] 审查 `docs` 目录下所有 `.md` 文件。

* [ ] 识别并移除重复的文档内容。

* [ ] 删除无关、过时或非必要的文档段落。

### 2.2 内容整合与结构优化

* [ ] 将相关的文档内容合并为结构清晰、逻辑连贯的综合文档（例如将算法类文档整合）。

* [ ] 确保所有文档使用统一的 Markdown 格式（标题级、列表、代码块规范等）。

***

## 3. 验证与交付

* [ ] 确保优化后的代码在编译时不引入新的警告或错误。

* [ ] 验证关键日志输出是否符合预期。

* [ ] 最终检查文档目录的整洁度。

