# 温湿度数据上传配置说明

## 功能概述

ESP32设备会将SHT30传感器读取的温湿度数据定期上传到你的云服务器。

## 数据格式

上传的JSON数据格式：

```json
{
  "device_id": "your-device-id",
  "temperature": 23.5,
  "humidity": 45.2,
  "timestamp": 1707532800
}
```

**字段说明：**
- `device_id`: 设备唯一标识符（可选）
- `temperature`: 温度值（摄氏度）
- `humidity`: 湿度值（百分比）
- `timestamp`: 时间戳（秒）

## HTTP请求配置

**请求方法：** POST

**请求头：**
```
Content-Type: application/json
Authorization: Bearer YOUR_API_KEY
```

**示例请求：**
```bash
curl -X POST https://your-server.com/api/sensor-data \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer your-api-key" \
  -d '{
    "device_id": "esp32-sht30-001",
    "temperature": 23.5,
    "humidity": 45.2,
    "timestamp": 1707532800
  }'
```

## 配置参数

在设备的NVS存储中配置以下参数：

| 参数名 | 说明 | 默认值 |
|--------|------|--------|
| `upload_url` | 上传服务器的URL | -（必填） |
| `api_key` | API认证密钥 | ""（可选） |
| `device_id` | 设备ID | ""（可选） |
| `upload_interval` | 上传间隔（秒） | 300（5分钟） |

## 配置方法

### 方法1：通过代码配置（推荐开发阶段）

在 `compact_wifi_board_lcd.cc` 的 `InitializeSensorUploader()` 函数中设置：

```cpp
sensor_uploader_->SetUploadUrl("https://your-server.com/api/sensor-data");
sensor_uploader_->SetApiKey("your-api-key");
sensor_uploader_->SetDeviceId("esp32-sht30-001");
sensor_uploader_->SetUploadInterval(300); // 5分钟
```

### 方法2：通过HTTP API配置

发送配置请求到设备（需要设备支持HTTP配置）：

```bash
curl -X POST http://device-ip/config \
  -d '{
    "upload_url": "https://your-server.com/api/sensor-data",
    "api_key": "your-api-key",
    "device_id": "esp32-sht30-001",
    "upload_interval": 300
  }'
```

### 方法3：通过串口配置

使用ESP32的串口控制台设置NVS：

```
nvs_set sensor_upload upload_url "https://your-server.com/api/sensor-data"
nvs_set sensor_upload api_key "your-api-key"
nvs_set sensor_upload device_id "esp32-sht30-001"
nvs_set sensor_upload upload_interval 300
nvs_commit
restart
```

## 上传策略

1. **自动上传**：每5分钟自动上传一次（可配置）
2. **去重机制**：如果温湿度数据变化不大（<0.1°C或<1%），跳过上传
3. **上传回调**：上传成功/失败会记录日志

## 日志输出

成功上传：
```
I (12345) CompactWifiBoardLCD: SHT30 read successful: temp=23.5°C, humi=45.2%
I (12350) SensorUpload: Built JSON: {"device_id":"esp32-sht30-001","temperature":23.5,"humidity":45.2,"timestamp":1707532800}
I (12355) SensorUpload: HTTP POST Status = 200, content_length = 25
I (12360) CompactWifiBoardLCD: Sensor data uploaded: Upload success, HTTP 200
```

上传失败：
```
W (12345) CompactWifiBoardLCD: SHT30 read successful: temp=23.5°C, humi=45.2%
E (12350) SensorUpload: HTTP POST request failed: ESP_ERR_HTTP_CONNECT
W (12355) CompactWifiBoardLCD: Sensor data upload failed: Upload failed: ESP_ERR_HTTP_CONNECT
```

## 服务器端示例

### Node.js (Express)

```javascript
const express = require('express');
const app = express();

app.use(express.json());

app.post('/api/sensor-data', (req, res) => {
  const { device_id, temperature, humidity, timestamp } = req.body;

  console.log(`收到数据: 设备=${device_id}, 温度=${temperature}°C, 湿度=${humidity}%`);

  // 存储到数据库
  saveToDatabase({ device_id, temperature, humidity, timestamp });

  res.json({ success: true });
});

app.listen(3000, () => {
  console.log('服务器运行在 http://localhost:3000');
});
```

### Python (Flask)

```python
from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/api/sensor-data', methods=['POST'])
def receive_sensor_data():
    data = request.json
    device_id = data.get('device_id')
    temperature = data.get('temperature')
    humidity = data.get('humidity')
    timestamp = data.get('timestamp')

    print(f"收到数据: 设备={device_id}, 温度={temperature}°C, 湿度={humidity}%")

    # 存储到数据库
    save_to_database(device_id, temperature, humidity, timestamp)

    return jsonify({'success': True})

if __name__ == '__main__':
    app.run(host='0.0.0.0.0', port=3000)
```

## 故障排除

### 问题1：配置不生效

**原因**：NVS中的配置未正确设置

**解决**：检查串口日志，确认"Sensor upload URL not configured"

### 问题2：上传失败

**可能原因**：
- 网络连接问题
- 服务器地址错误
- API Key无效
- 服务器未响应

**解决方法**：
1. 检查网络连接状态
2. 验证upload_url是否正确
3. 确认API Key是否有效
4. 查看服务器日志

### 问题3：数据不准确

**原因**：传感器校准问题

**解决**：调整 `compact_wifi_board_lcd.cc` 中的偏移量：
```cpp
sht30_sensor_->SetTemperatureOffset(-1.0f); // 温度偏移-1度
sht30_sensor_->SetHumidityOffset(0.0f);   // 湿度偏移0度
```

## 安全建议

1. **使用HTTPS**：生产环境建议使用HTTPS加密传输
2. **API Key保护**：不要在代码中硬编码API Key
3. **数据验证**：服务器端验证数据范围（温度-40~80°C，湿度0~100%）
4. **限流保护**：服务器端设置请求频率限制

## 文件清单

| 文件 | 说明 |
|------|------|
| `main/sensor_upload.h` | 传感器上传类定义 |
| `main/sensor_upload.cc` | 传感器上传类实现 |
| `main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc` | 板级集成 |
| `SENSOR_UPLOAD_README.md` | 本文档 |

## 版本历史

### v1.0.0 (2026-02-10)
- 初始版本
- 支持HTTP POST上传温湿度数据
- 支持配置上传URL、API Key、设备ID、上传间隔
- 支持去重机制
- 支持上传回调
