# WeatherClock

基于 `STM32F407 + FreeRTOS + ST7789 + ESP-AT + AHT20` 的天气时钟练手项目。

## 最近更新

- 最后更新时间：`2026-07-09 10:59:12 +08:00`
- 本次更新内容：
  - 把 WiFi 热点配置从业务逻辑中拆到独立配置头文件
  - 新增 `wifi_config.example.h` 示例文件，并将真实配置文件加入忽略规则
  - 扩展 `ESP-AT` 驱动，支持 `ATE0`、热点连接和 `AT+CIFSR` 取 IP
  - 更新 `ESP AT TEST` 页面，按 `ESP AT -> GMR -> MODE -> WIFI -> IP` 分阶段显示结果

## 项目目录

- `app/`：主流程、页面逻辑、测试入口、WiFi 配置
- `driver/`：`ST7789`、`AHT20`、`ESP-AT`、`RTC` 等驱动
- `firmware/`：`CMSIS` 与 `STM32F4xx Standard Peripheral Library`
- `third_lib/`：`FreeRTOS`
- `resources/`：图片、字体和设计资源
- `mdk/`：Keil 工程文件与编译输出

## 硬件接线说明

### ST7789 SPI 屏幕接线

| 屏幕引脚 | STM32F407 核心板 |
| ---- | ---- |
| GND  | GND  |
| VCC  | 3V3  |
| SCL  | PB13 |
| SDA  | PC3  |
| RES  | PE3  |
| DC   | PE4  |
| CS   | PE2  |
| BLK  | 3V3  |

### AHT20 接线

| AHT20 引脚 | STM32F407 核心板 |
| ---- | ---- |
| VIN  | 3V3  |
| GND  | GND  |
| SCL  | PB10 |
| SDA  | PB11 |

### ESP32-C3 ESP-AT 接线

| ESP32-C3 | STM32F407 核心板 |
| ---- | ---- |
| GND | GND |
| GPIO6 / AT_RX | PA2 / USART2_TX |
| GPIO7 / AT_TX | PA3 / USART2_RX |

### 供电说明

- 当前 `ESP32-C3` 使用 `USB-C` 单独供电
- `STM32F407` 核心板单独供电
- 两边目前只共 `GND`
- 当前阶段不要再从 STM32 核心板 `5V` 给 `ESP32-C3` 供电

### 引脚说明

- `ST7789 SCL` 是 `SPI SCK`，不是 `I2C SCL`
- `ST7789 SDA` 是 `SPI MOSI`，不是 `I2C SDA`
- `BLK` 当前固定接 `3V3`，背光常亮，便于排查显示内容
- `ESP32-C3` 当前与 STM32 通信的有效引脚是 `GPIO6/GPIO7`
- `GPIO20/GPIO21` 不再作为当前项目的 STM32 通信接线使用
- STM32 端固定使用 `USART2`，其中 `PA2` 为 `TX`，`PA3` 为 `RX`
- 所有 `AT` 命令都必须以 `\r\n` 结尾

## 调试结论

### 屏幕部分

- 当前项目代码使用 `PB13` 作为 `SPI` 时钟线，使用 `PC3` 作为 `SPI` 数据线
- `ST7789` 基础自检已经通过，黑、白、红、绿、蓝页面显示正常
- 英文、数字和简单图形显示正常
- 说明 `SPI` 通信、屏幕初始化、`RES/DC/CS` 控制线和基础显示方向已验证通过

### 传感器部分

- `AHT20` 当前已调通
- 但本阶段不对 `AHT20` 做修改

### ESP-AT 部分

- 当前 `ESP32-C3` 已烧录 `ESP-AT` 固件
- 串口助手测试 `AT` 已返回 `OK`
- 串口助手测试 `AT+GMR` 已返回版本信息
- 当前与 STM32 通信的连接以 `GPIO6/GPIO7 <-> PA2/PA3` 为准
- STM32 端驱动使用 `USART2 115200 8N1`，无校验、1 位停止位、无流控

### 当前阶段目标

本阶段只验证：

- `ESP32-C3 ESP-AT` 成功连接手机热点
- 成功获取 IP 地址
- 屏幕与串口都能看到每一步结果

## WiFi 配置说明

当前热点配置文件位于：

- [`app/wifi_config.h`](/D:/1_codingSoftware/4_Keil_v5/Projects/WeatherClock-main/WeatherClock-main/app/wifi_config.h)
- [`app/wifi_config.example.h`](/D:/1_codingSoftware/4_Keil_v5/Projects/WeatherClock-main/WeatherClock-main/app/wifi_config.example.h)

当前格式为：

```c
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

说明：

- `wifi_config.h` 用于本地真实配置
- `wifi_config.example.h` 用于示例和仓库共享
- `.gitignore` 已忽略 `app/wifi_config.h`
- 上传代码前请确认不要提交真实热点名称和密码

## 当前可切换测试入口

当前在 [`app/main.c`](/D:/1_codingSoftware/4_Keil_v5/Projects/WeatherClock-main/WeatherClock-main/app/main.c) 中保留了四个独立测试开关：

- `ENABLE_LCD_SELF_TEST`
- `ENABLE_AHT20_LCD_TEST`
- `ENABLE_WIFI_SNTP_LCD_TEST`
- `ENABLE_ESP_AT_BASIC_TEST`

建议当前阶段配置：

- `ENABLE_LCD_SELF_TEST = 0`
- `ENABLE_AHT20_LCD_TEST = 0`
- `ENABLE_WIFI_SNTP_LCD_TEST = 0`
- `ENABLE_ESP_AT_BASIC_TEST = 1`

这样上电后会直接进入 `ESP AT TEST` 页面，先跑：

- `AT`
- `AT+GMR`
- `ATE0`
- `AT+CWMODE=1`
- `AT+CWJAP="WIFI_SSID","WIFI_PASSWORD"`
- `AT+CIFSR`

## 当前实现状态

### 已完成

- `FreeRTOS` 主框架可启动
- `ST7789` 屏幕已调通
- 屏幕基础自检正常
- `AHT20` 温湿度读取正常
- `ESP32-C3 ESP-AT` 基础串口测试已通过
- `ESP32-C3` 热点连接与取 IP 测试入口已接入

### 当前屏幕显示目标

烧录后，屏幕应分阶段显示：

- `ESP AT: OK`
- `GMR: OK`
- `MODE: OK`
- `WIFI: OK`
- `IP: xxx.xxx.xxx.xxx`

如果失败，屏幕会在进度行显示明确阶段，例如：

- `ATE0 FAIL`
- `MODE FAIL`
- `WIFI FAIL`
- `IP FAIL`

### 当前串口日志目标

串口应看到类似日志：

```text
[ESP TEST] start
[ESP TEST] UART2 init: 115200 8N1
[ESP TEST] STM32 PA2 / USART2_TX -> ESP32-C3 GPIO6 / AT_RX
[ESP TEST] STM32 PA3 / USART2_RX -> ESP32-C3 GPIO7 / AT_TX
[ESP TEST] wait ESP boot...
[ESP TEST] Send: AT
[ESP TEST] Response:
OK
[ESP TEST] Send: AT+GMR
[ESP TEST] Response:
AT version:4.1.1.0
SDK version:v5.4.1
Bin version:v4.1.0(MINI-1)
OK
[ESP TEST] Send: ATE0
[ESP TEST] Response:
OK
[ESP TEST] Send: AT+CWMODE=1
[ESP TEST] Response:
OK
[ESP TEST] Send: AT+CWJAP="你的热点名","******"
[ESP TEST] Response:
...
[ESP TEST] WIFI JOIN OK
[ESP TEST] Send: AT+CIFSR
[ESP TEST] Response:
...
[ESP TEST] IP OK
[ESP TEST] IP: xxx.xxx.xxx.xxx
```

## 手机热点注意事项

- 热点名称建议先用英文或数字
- 热点密码建议先用英文或数字
- 手机热点建议优先开启 `2.4GHz` 或兼容模式
- 如果热点名或密码包含特殊字符，建议先改成简单版本再测试

## 常见注意事项

- 背光亮不代表屏幕通信成功，必须看到颜色、文字或图形才算链路正常
- 如果当前只做 `ESP-AT` 测试失败，优先检查 `GND` 是否共地、`PA2->GPIO6`、`PA3->GPIO7` 是否交叉正确
- 如果 `WIFI JOIN FAIL`，优先检查热点是否开启、是否是 `2.4GHz`、热点名和密码是否正确
- 如果 `IP FAIL`，优先检查热点是否真的分配了地址，或者 `AT+CIFSR` 响应格式是否和当前固件版本一致
- 后续继续改代码前，建议保留当前“屏幕正常、AT 通信正常、热点连接可测”的版本作为稳定回退点

## 下一步计划

- 先确认 `ESP32-C3` 连接热点并成功获取 IP
- 再打开 `ENABLE_WIFI_SNTP_LCD_TEST`，继续验证 `WiFi + SNTP`
- 最后再恢复完整天气时钟主流程和天气 API 测试

## 参考视频

[【全开源手把手项目实战】基于 FreeRTOS 的 STM32 智能天气时钟](https://www.bilibili.com/video/BV1tfL1zeEQN)

## 联系方式

添加 UP 主微信，获取完整项目资料和学习路线，进交流群讨论项目内容。

![vxcode](docs/image/vxcode.jpg)
