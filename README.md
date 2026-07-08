# WeatherClock

基于 `STM32F407 + FreeRTOS + ST7789 + ESP-AT + AHT20` 的天气时钟练手项目。

## 最近更新

- 最后更新时间：`2026-07-08 20:30:58 +08:00`
- 本次更新内容：
  - 保留了 `ST7789` 屏幕基础自检入口
  - 保留了 `AHT20` 本地温湿度屏幕测试入口
  - 新增了“第五步：WiFi + SNTP 时间显示测试”入口
  - 屏幕可独立显示 `WiFi` 状态、`SNTP` 状态、日期、时间和室内温湿度

## 项目目录

- `app/`：主流程、页面逻辑、测试入口
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

### 引脚说明

- `ST7789 SCL` 是 `SPI SCK`，不是 `I2C SCL`
- `ST7789 SDA` 是 `SPI MOSI`，不是 `I2C SDA`
- `BLK` 当前固定接 `3V3`，背光常亮，便于排查显示内容
- 当前外接屏幕应以本 README 接线表和项目代码为准，不要直接套用某些板载 TFT 默认引脚说明

## 调试结论

### 屏幕部分

- 当前项目代码使用 `PB13` 作为 `SPI` 时钟线，使用 `PC3` 作为 `SPI` 数据线
- `ST7789` 基础自检已经通过，黑、白、红、绿、蓝页面显示正常
- 英文、数字和简单图形显示正常
- 说明 `SPI` 通信、屏幕初始化、`RES/DC/CS` 控制线和基础显示方向已验证通过

### 传感器部分

- `AHT20` 已经完成本地温湿度读取
- 串口可以打印温湿度
- 屏幕可以显示 `Indoor Temp`、`Humidity` 和 `AHT20 OK`

### WiFi / 时间部分

- 项目中已复用现有 `ESP-AT`、`SNTP` 和 `RTC` 代码
- 第五步当前用于独立验证：
  - `ESP8266/ESP-AT` 联网
  - `SNTP` 同步时间
  - `RTC` 保存并递增本地时间
  - `ST7789` 显示 `WiFi` 状态、`SNTP` 状态、日期和时间

## 当前可切换测试入口

当前在 [`app/main.c`](/D:/1_codingSoftware/4_Keil_v5/Projects/WeatherClock-main/WeatherClock-main/app/main.c) 中保留了三个独立测试开关：

- `ENABLE_LCD_SELF_TEST`
- `ENABLE_AHT20_LCD_TEST`
- `ENABLE_WIFI_SNTP_LCD_TEST`

建议当前阶段配置：

- `ENABLE_LCD_SELF_TEST = 0`
- `ENABLE_AHT20_LCD_TEST = 0`
- `ENABLE_WIFI_SNTP_LCD_TEST = 1`

这样上电后会直接进入 `WiFi + SNTP + LCD` 测试页面。

## WiFi 配置说明

- 当前 `WiFi SSID` 和密码配置位于 [`app/wifi.h`](/D:/1_codingSoftware/4_Keil_v5/Projects/WeatherClock-main/WeatherClock-main/app/wifi.h)
- 目前项目仍是源码内硬编码方式
- 如果后续要上传到云端仓库，建议不要提交真实密码
- 建议上传前改成模板，例如：

```c
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASSWD "YOUR_WIFI_PASSWORD"
```

## 当前实现状态

### 已完成

- `FreeRTOS` 主框架可启动
- `ST7789` 屏幕已调通
- 屏幕基础自检正常
- `AHT20` 温湿度读取正常
- `AHT20 -> 串口 -> ST7789` 本地显示链路已打通
- `WiFi + SNTP + RTC + LCD` 测试入口已接入

### 当前第五步目标

烧录后，屏幕应能显示：

- `WiFi: Connecting...` / `WiFi: OK` / `WiFi: ERROR`
- `SNTP: Syncing...` / `SNTP: OK` / `SNTP: ERROR`
- 当前时间：`HH:MM:SS`
- 当前日期：`YYYY-MM-DD`
- 室内温湿度：`xx.x C`、`xx.x %`

串口应能看到类似日志：

```text
[WIFI TEST] start
[WIFI TEST] ESP init...
[WIFI TEST] WiFi connecting...
[WIFI TEST] WiFi connected
[SNTP] syncing...
[SNTP] sync ok: YYYY-MM-DD HH:MM:SS
[AHT20] Temp: xx.x C, Humi: xx.x %
```

## 常见注意事项

- 背光亮不代表屏幕通信成功，必须看到颜色、文字或图形才算链路正常
- 如果屏幕只有背光没有内容，优先检查 `SCL`、`SDA`、`RES`、`DC`、`CS`
- 如果 `WiFi` 连接失败，优先检查 `ESP8266` 供电、串口连线、波特率和 `AT` 固件状态
- 如果 `SNTP` 失败但 `WiFi` 正常，优先检查网络是否可访问外网时间服务
- 如果后续继续改代码，建议先保留当前“屏幕/AHT20/WiFi-SNTP 测试可用”的版本作为稳定回退点

## 下一步计划

- 在稳定的 `WiFi + SNTP` 基础上，继续恢复完整天气时钟主流程
- 接入天气 API，但与当前网络时间测试分开验证
- 后续再考虑城市切换、设置页、参数持久化等功能

## 参考视频

[【全开源手把手项目实战】基于 FreeRTOS 的 STM32 智能天气时钟](https://www.bilibili.com/video/BV1tfL1zeEQN)

## 联系方式

添加 UP 主微信，获取完整项目资料和学习路线，进交流群讨论项目内容。

![vxcode](docs/image/vxcode.jpg)
