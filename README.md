# HomeworkTimer for ESP32-S3-Touch-LCD-3.49B

面向儿童家庭作业管理的独立 ESP-IDF/LVGL 工程，适配微雪
`ESP32-S3-Touch-LCD-3.49B`（172 × 640，横屏逻辑分辨率 640 × 172）。

## 功能

- 语文、数学、英语分别开始、暂停、继续和完成计时
- 休息时间独立记录
- NVS 保存各科学习时间、休息时间和完成次数
- 每天记录三科完成状态，跨日自动更新
- 每周一自动换周，并保留上一周数据快照
- 周报显示各科累计、专注总时间及休息时间
- 使用板载 PCF85063 RTC；RTC 无有效时间时以固件编译时间初始化
- 内置仅包含界面文字的精简中文字库

已完成的计时段会立即写入 NVS。正在进行但尚未点击“完成”的计时段，断电后不会保留。

## 工程结构

```text
main/
├── app_main.c          启动入口
├── board_display.*     QSPI 屏幕、触摸、背光及 LVGL 任务
├── rtc_pcf85063.*      RTC 驱动
├── study_store.*       周期计算及 NVS 数据持久化
├── study_ui.*          LVGL 页面和计时交互
├── ui_font_16.c        精简中文字库
└── board_config.h      引脚和显示参数
```

LVGL 和 AXS15231B 面板驱动由 ESP-IDF Component Manager 下载，未把第三方源码复制进仓库。

## 环境

- ESP-IDF 5.4 或更新版本
- 已在 ESP-IDF 6.0.2 下进行构建验证
- 目标芯片：ESP32-S3
- Flash：16 MB
- PSRAM：8 MB OPI

## 构建

在 ESP-IDF 命令行中进入工程目录：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM15 flash monitor
```

请将 `COM15` 改为设备实际串口。工程首次构建会通过组件管理器下载固定版本的
LVGL 9.3.0 和 `esp_lcd_axs15231b` 2.1.0。

## 后续扩展建议

- 在 `study_store` 中增加每天的历史数组，可绘制一周柱状图
- 加入 Wi-Fi/SNTP 校时，保留 RTC 作为离线时钟
- 增加课程表和家长设置页
- 定时把统计数据导出为 JSON 或通过局域网页面查看

## 来源与许可证

硬件引脚、面板初始化方式和触摸读取协议参考微雪官方
[ESP32-S3-Touch-LCD-3.49](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49)
示例。应用代码以 MIT License 发布；通过组件管理器取得的第三方组件遵循各自许可证。
