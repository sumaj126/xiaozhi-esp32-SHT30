# SHT30温湿度传感器集成文档

## 概述

本项目已集成SHT30温湿度传感器，通过UART通信获取实时温湿度数据，并在待机界面显示。

## 硬件连接

| 传感器引脚 | ESP32引脚 | 说明 |
|-----------|-----------|------|
| TX | GPIO17 | 传感器发送数据 |
| RX | GPIO18 | 传感器接收数据 |
| VCC | 3.3V/5V | 电源正极 |
| GND | GND | 电源负极 |

**UART配置：**
- 端口：UART_NUM_2
- 波特率：9600
- 数据位：8
- 停止位：1
- 校验位：无

## 数据格式

SHT30通过UART自动上报数据，格式为：
```
R:039.2RH 023.3C
```

- `039.2RH`：湿度值（39.2%）
- `023.3C`：温度值（23.3°C）

## 软件实现

### 1. 传感器驱动类

位置：`main/boards/common/sht30_sensor.h`

主要功能：
- 初始化UART通信
- 解析传感器数据
- 提供温度校准和湿度校准
- 支持JSON格式数据输出

```cpp
// 初始化传感器
SHT30Sensor sensor(UART_NUM_2, 17, 18, 9600);

// 读取温湿度
float temp, humidity;
if (sensor.ReadData(&temp, &humidity)) {
    printf("温度: %.1f°C, 湿度: %.1f%%\n", temp, humidity);
}

// 校准温度（offset偏移）
sensor.CalibrateTemperature(25.0f);  // 设置实际温度25°C

// 获取JSON数据
std::string json = sensor.GetJsonData();
```

### 2. 待机界面显示

位置：`main/display/standby_screen.cc`

显示内容：
- 当前时间（大字体，屏幕中央偏左）
- 日期（顶部居中）
- 星期（日期下方居中）
- 温度（左下角）
- 湿度（右下角）

### 3. 颜色显示规则

**温度颜色：**
- **蓝色** (`0x2196F3`)：< 20°C
- **渐变色** (浅黄→深橙)：20°C ~ 30°C
  - 浅黄色 (`0xFFEB3B`) @ 20°C
  - 深橙色 (`0xFF5722`) @ 30°C
- **红色** (`0xF44336`)：> 30°C

**湿度颜色：**
- **绿色** (`0x4CAF50`)：始终显示绿色

### 4. 设备状态监控

位置：`main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc`

实现自动界面切换：
- **空闲状态**：显示待机界面
- **对话状态**（录音/播放）：隐藏待机，显示对话界面
- **配置状态**（WiFi配置）：隐藏待机，显示配置界面
- **连接状态**（连接/激活）：隐藏待机，显示连接状态

状态监控定时器：每秒检查一次

## MCP工具集成

SHT30传感器已注册为MCP工具，可供AI调用：

**工具名称：** `sensor.read_temperature_humidity`

**描述：** 读取当前环境的温度和湿度数据

**返回格式：**
```json
{
  "temperature": 23.3,
  "humidity": 39.2
}
```

**AI调用示例：**
```
用户：现在温度多少？
AI：正在读取传感器数据...
AI：当前温度为23.3°C，湿度为39.2%。
```

## 校准方法

### 温度校准

使用标准温度计测量实际温度，然后调用校准函数：

```cpp
// 假设实际温度为25.0°C，传感器读取为24.5°C
sensor.CalibrateTemperature(25.0f);
// 系统会自动计算偏移量：25.0 - 24.5 = +0.5°C
```

### 湿度校准

使用标准湿度计测量实际湿度，然后调用校准函数：

```cpp
// 假设实际湿度为50.0%，传感器读取为48.0%
sensor.CalibrateHumidity(50.0f);
// 系统会自动计算偏移量：50.0 - 48.0 = +2.0%
```

### 手动设置偏移量

```cpp
// 温度偏移 +1.0°C
sensor.SetTemperatureOffset(1.0f);

// 湿度偏移 -5.0%
sensor.SetHumidityOffset(-5.0f);
```

## 故障排除

### 传感器未连接

如果传感器未连接或初始化失败，待机界面会显示占位符：
- 温度：`--.-°C`
- 湿度：`--.-%`

### 日志输出

系统会输出详细的调试日志：

```
I (1234) SHT30: SHT30 initialized on UART_NUM_2 (TX:GPIO17, RX:GPIO18, 9600 baud)
I (2345) SHT30: Temperature: 23.30°C, Humidity: 39.20%
I (3456) CompactWifiBoardLCD: SHT30 read successful: temp=23.3°C, humi=39.2%
I (4567) StandbyScreen: Updating temperature/humidity UI: 23.3°C 39.2%
```

### 常见问题

1. **数据读取失败**
   - 检查UART引脚连接是否正确
   - 确认传感器供电正常
   - 检查波特率是否设置为9600

2. **显示异常**
   - 检查传感器是否正常输出数据
   - 查看日志中的解析警告信息

3. **温度/湿度不准**
   - 使用校准功能调整偏移量
   - 确认传感器工作环境稳定

## 文件清单

| 文件 | 说明 |
|------|------|
| `main/boards/common/sht30_sensor.h` | SHT30传感器驱动类 |
| `main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc` | 板级集成代码 |
| `main/display/standby_screen.cc` | 待机界面显示逻辑 |
| `main/boards/bread-compact-wifi-lcd/config.h` | 配置定义 |

## 版本历史

### v1.0.0 (2026-02-09)
- 初始版本
- 集成SHT30温湿度传感器
- 实现待机界面温湿度显示
- 添加温度颜色渐变功能
- 实现MCP工具集成
- 完善校准功能

## 许可证

遵循项目主许可证。
