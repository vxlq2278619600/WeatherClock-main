# WeatherClock（STM32 天气时钟）

基于 **STM32F407 + FreeRTOS** 的桌面天气时钟。项目通过 ESP32-C3 的 ESP-AT 固件连接 Wi-Fi、同步网络时间并请求高德天气数据，同时使用 AHT20 采集室内温湿度，最终在 2.4 英寸 ST7789 彩屏上显示时间、日期、室内环境和室外天气。

> 当前 `app/main.c` 默认启用的是 `ESP-AT` 综合测试流程，适合分阶段检查联网、时间、传感器和天气接口；如需恢复原始应用框架，可调整文件顶部的测试开关。

## 功能概览

- ST7789 240 × 320 彩屏界面，支持文字、图片及天气图标
- AHT20 室内温度、湿度采集
- ESP32-C3（ESP-AT）连接 2.4 GHz Wi-Fi
- SNTP 网络校时，并将时间写入 STM32 RTC
- 通过高德开放平台天气 API 获取城市实况天气
- 主界面、网络、传感器、天气四个信息页，按下 PA0 可切换页面
- FreeRTOS 任务、软件定时器及工作队列
- LCD、AHT20、Wi-Fi/SNTP、ESP-AT 独立测试入口
- USART1（115200 8N1）调试日志

## 硬件组成

| 模块 | 说明 |
| --- | --- |
| MCU | STM32F407 |
| 显示屏 | ST7789，240 × 320，SPI2 |
| 无线模块 | ESP32-C3，运行 ESP-AT 固件 |
| 温湿度传感器 | AHT20，I2C1 |
| 实时时钟 | STM32 内部 RTC |
| 外部存储 | BL24C512 EEPROM（驱动已包含） |
| 操作按键 | PA0，高电平按下 |

## 接线说明

### ST7789 显示屏

| ST7789 | STM32F407 | 说明 |
| --- | --- | --- |
| GND | GND | 地 |
| VCC | 3V3 | 电源 |
| SCL / SCK | PB13 | SPI2_SCK |
| SDA / MOSI | PC3 | SPI2_MOSI |
| CS | PE2 | 片选 |
| RES | PE3 | 复位 |
| DC | PE4 | 数据/命令选择 |
| BLK | PE5 | 背光控制 |

### AHT20

| AHT20 | STM32F407 |
| --- | --- |
| VIN | 3V3 |
| GND | GND |
| SCL | PB6 / I2C1_SCL |
| SDA | PB7 / I2C1_SDA |

### ESP32-C3（ESP-AT）

| ESP32-C3 | STM32F407 |
| --- | --- |
| GND | GND |
| GPIO6 / AT_RX | PA2 / USART2_TX |
| GPIO7 / AT_TX | PA3 / USART2_RX |

串口参数为 `115200 8N1`，无校验、无流控。ESP32-C3 可使用 USB-C 独立供电，但必须与 STM32 共地；请根据模块规格确认供电方式，不要同时从多个电源端反向供电。

## 软件结构

```text
WeatherClock-main/
├─ app/                 应用入口、业务流程、页面、字体及图片数据
│  ├─ page/             欢迎页、主页、Wi-Fi 页和错误页
│  ├─ font/             点阵字体资源
│  └─ image/            天气图标及界面图片
├─ driver/              ST7789、ESP-AT、AHT20、RTC、EEPROM 等驱动
├─ firmware/            CMSIS 与 STM32F4 标准外设库
├─ third_lib/freertos/  FreeRTOS 内核及移植层
├─ resources/           原始图片、界面设计稿
├─ tools/               中文字体转换工具
└─ mdk/                 Keil MDK 工程文件
```

主要数据流程：

```text
AHT20 ──I2C──> STM32F407 ──SPI──> ST7789
                         │
                         └─USART2──> ESP32-C3 ──Wi-Fi──> SNTP / 高德天气 API
```

## 快速开始

### 1. 准备开发环境

- Keil MDK-ARM 5
- 支持 STM32F407 的 Device Pack
- ST-Link 或兼容下载器
- 已刷入 ESP-AT 固件的 ESP32-C3
- 可用的 2.4 GHz Wi-Fi 或手机热点

### 2. 配置 Wi-Fi

复制 `app/wifi_config.example.h` 为 `app/wifi_config.h`，填写实际热点信息：

```c
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

### 3. 配置天气服务

在高德开放平台创建 **Web 服务** Key，然后复制 `app/weather_config.example.h` 为 `app/weather_config.h`：

```c
#define WEATHER_API_KEY       "YOUR_AMAP_WEB_SERVICE_KEY"
#define WEATHER_CITY_ADCODE   "610100"
#define WEATHER_CITY_NAME     "Xi'an"
```

`WEATHER_CITY_ADCODE` 应填写目标城市的行政区划代码。当前天气请求默认使用 `restapi.amap.com` 的 HTTP 接口；如需 SSL，可在构建配置中定义 `WEATHER_USE_SSL=1`，并确保 ESP-AT 固件支持相应连接。

`wifi_config.h` 与 `weather_config.h` 已加入 `.gitignore`，上传仓库前请勿把真实 SSID、密码或 API Key 写入示例文件。

### 4. 选择运行模式

在 `app/main.c` 顶部配置以下开关，同一时间建议只启用一种测试：

| 开关 | 用途 |
| --- | --- |
| `ENABLE_LCD_SELF_TEST` | LCD 颜色、文字和图形自检 |
| `ENABLE_AHT20_LCD_TEST` | AHT20 温湿度显示测试 |
| `ENABLE_WIFI_SNTP_LCD_TEST` | Wi-Fi、SNTP 与 RTC 测试 |
| `ENABLE_ESP_AT_BASIC_TEST` | ESP-AT 联网综合测试 |

仓库当前默认配置为：

```c
#define ENABLE_LCD_SELF_TEST       0
#define ENABLE_AHT20_LCD_TEST      0
#define ENABLE_WIFI_SNTP_LCD_TEST  0
#define ENABLE_ESP_AT_BASIC_TEST   1
```

该模式会依次检查 AT 通信、固件版本、Wi-Fi 连接、IP、SNTP、AHT20 和天气接口；运行后短按 PA0 可切换信息页。

### 5. 编译与烧录

1. 使用 Keil 打开 `mdk/stm32f407.uvprojx`。
2. 确认目标芯片、下载器及本地配置头文件已就绪。
3. Build 工程并通过 ST-Link 下载到开发板。
4. 打开 USART1 串口终端，使用 `115200 8N1` 查看运行日志。

## 界面与资源

`resources/design/` 中保存了欢迎页、主页、Wi-Fi 连接页和错误页设计稿；`resources/images/` 保存原始图像，转换后的 C 数组位于 `app/image/`。天气图标覆盖晴、夜间、多云、阴、雨、雷雨、雪及未知状态。

## 常见问题

- **屏幕背光亮但无内容**：检查 PB13、PC3、PE2～PE5 接线；背光亮不代表 SPI 通信成功。
- **ESP-AT 无响应**：确认 PA2 接 GPIO6、PA3 接 GPIO7，TX/RX 已交叉连接且两块板共地。
- **Wi-Fi 连接失败**：优先使用 2.4 GHz 热点，并用简单的英文/数字 SSID 和密码排除转义问题。
- **SNTP 失败**：先确认模块已取得 IP，再检查网络是否允许访问时间服务器。
- **天气请求失败**：检查 Web 服务 Key、城市 adcode、网络状态及串口中的 HTTP 响应。
- **AHT20 初始化失败**：确认源码当前使用 PB6/PB7，而不是其他 I2C2 引脚，并检查 3.3 V 供电。
- **编译提示缺少配置文件**：从两个 `.example.h` 文件复制生成本地配置文件。

## 当前状态

目前已具备 LCD、AHT20、ESP-AT、Wi-Fi、SNTP/RTC、高德实况天气和多页面显示代码，并保留了便于硬件逐项排查的测试模式。项目仍属于开发与调试阶段，实际稳定性会受到 ESP-AT 固件版本、网络环境、模块供电及硬件接线影响。

## License

本项目许可证见 [LICENSE](LICENSE)。
