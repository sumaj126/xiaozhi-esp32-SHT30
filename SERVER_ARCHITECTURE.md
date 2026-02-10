# ESP32 传感器系统服务器架构文档

## 一、整体架构概览

```
ESP32设备 (3台) → Nginx反向代理 → Node.js微服务 → 前端展示
                        ↓                    ↓
                    MQTT Broker         数据存储
```

### 1.1 系统组成

本系统包含三个房间的环境监控：

| 房间 | ESP32设备 | 上传端口 | Nginx映射 | 服务端口 | 监控页面 |
|------|-----------|----------|-----------|----------|----------|
| 客厅/书房 | ESP32-main | 7788 | 3788 | 3788 | `/var/www/html/index.html` |
| 办公室 | ESP32-office | 7789 | 3789 | 3789 | `/opt/office-receiver/index.html` |
| 电脑室 | ESP32-pcroom | 7791 | 3791 | 3791 | `/opt/pcroom-receiver/index.html` |

### 1.2 端口映射关系

- **ESP32上传端口** (7788/7789/7791): ESP32设备发送HTTP POST数据到这些端口
- **Nginx映射端口** (3788/3789/3791): Nginx反向代理将请求转发到Node.js服务
- **Node.js服务端口** (3788/3789/3791): 实际运行的数据接收和处理服务

---

## 二、核心组件详解

### 2.1 ESP32端 (固件层)

#### 主要文件：
- `main/sensor_upload.cc`: 传感器数据上传功能实现
- `main/sensor_upload.h`: SensorDataUploader类声明
- `main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc`: 主控制逻辑

#### 关键配置：
```cpp
// 传感器上传间隔（秒）
static constexpr int SENSOR_UPLOAD_INTERVAL = 60;  // 客厅：60秒（避免与小智冲突）
// ESP32-office: 10秒
// ESP32-pcroom: 10秒

// HTTP上传超时（毫秒）
static constexpr int UPLOAD_TIMEOUT_MS = 2000;
```

#### 上传数据格式：
```json
{
  "temperature": 25.6,
  "humidity": 65.3,
  "timestamp": 1739187360
}
```

#### MCP工具集成：
```cpp
// 小智语音助手读取传感器数据的MCP工具
mcp_server.AddTool("sensor.read_temperature_humidity",
    "读取当前环境的温度和湿度数据",
    PropertyList(),
    [this](const PropertyList& properties) -> ReturnValue {
        cJSON* result = cJSON_CreateObject();
        if (result) {
            float temp = sht30_sensor_->GetTemperature();
            float hum = sht30_sensor_->GetHumidity();
            cJSON_AddNumberToObject(result, "temperature", temp);
            cJSON_AddNumberToObject(result, "humidity", hum);
        }
        return result;
    });
```

**重要说明**：
- MCP工具返回缓存的传感器值，避免频繁读取UART导致冲突
- ESP32-main使用60秒上传间隔，避免阻塞小智唤醒词检测
- HTTP上传超时2秒，超时后不阻塞系统

---

### 2.2 Nginx反向代理层

#### 配置文件位置：
- `/etc/nginx/conf.d/esp32-sensors.conf`

#### 配置示例：
```nginx
# 客厅/书房监控
server {
    listen 7788;
    location / {
        proxy_pass http://127.0.0.1:3788;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}

# 办公室监控
server {
    listen 7789;
    location / {
        proxy_pass http://127.0.0.1:3789;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}

# 电脑室监控
server {
    listen 7791;
    location / {
        proxy_pass http://127.0.0.1:3791;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}

# 前端静态文件服务
server {
    listen 80;
    root /var/www/html;
    index index.html;
}
```

**作用说明**：
- 端口转发：将外部端口(7788/7789/7791)转发到内部Node.js服务(3788/3789/3791)
- 安全性：Node.js服务不直接暴露到外网
- 负载均衡：支持未来扩展多个实例

---

### 2.3 Node.js微服务层

#### 服务1: 客厅/书房接收服务
**文件位置**: `/opt/esp32-receiver/server.js`

**核心功能**：
- 接收ESP32-main的温湿度数据
- 通过MQTT发布空调控制消息
- 提供HTTP API获取最新数据

**离线阈值**: 90秒

**服务管理**:
```bash
systemctl status esp32-receiver
systemctl start esp32-receiver
systemctl stop esp32-receiver
```

#### 服务2: 办公室接收服务
**文件位置**: `/opt/office-receiver/server.js`

**核心功能**：
- 接收ESP32-office的温湿度数据
- 提供HTTP API获取最新数据

**离线阈值**: 90秒

**服务管理**:
```bash
systemctl status office-receiver
systemctl start office-receiver
systemctl stop office-receiver
```

#### 服务3: 电脑室接收服务
**文件位置**: `/opt/pcroom-receiver/server.js`

**核心功能**：
- 接收ESP32-pcroom的温湿度数据
- 提供HTTP API获取最新数据
- **不包含**空调控制功能

**离线阈值**: 180秒

**服务管理**:
```bash
systemctl status pcroom-receiver
systemctl start pcroom-receiver
systemctl stop pcroom-receiver
```

#### 统一的服务代码结构：
```javascript
const express = require('express');
const app = express();
app.use(express.json());

// 全局变量存储最新数据
let latestData = {
    temperature: null,
    humidity: null,
    timestamp: 0
};

// 接收传感器数据端点
app.post('/', (req, res) => {
    const { temperature, humidity } = req.body;
    latestData = {
        temperature: parseFloat(temperature).toFixed(1),
        humidity: parseFloat(humidity).toFixed(1),
        timestamp: Math.floor(Date.now() / 1000)
    };
    res.json({ success: true });
});

// 获取传感器数据端点
app.get('/data', (req, res) => {
    res.json(latestData);
});

// 启动服务
const PORT = 3788;  // 对应服务端口
app.listen(PORT, () => {
    console.log(`Sensor receiver running on port ${PORT}`);
});
```

---

### 2.4 MQTT消息代理层

#### MQTT Broker:
- **服务**: Mosquitto MQTT Broker
- **端口**: 1883
- **用途**: 设备间消息通信

#### 主要Topic:
- `ac/livingroom/control`: 客厅空调控制
- `ac/office/control`: 办公室空调控制

#### 消息格式:
```json
{
  "action": "on",
  "temperature": 26,
  "mode": "cool"
}
```

---

### 2.5 前端展示层

#### 客厅监控页面
**文件位置**: `/var/www/html/index.html`

**功能**：
- 显示客厅/书房的温湿度
- 空调控制按钮（开关、温度调节、模式切换）
- 设备在线状态显示
- 移动端响应式布局

**响应式CSS示例**：
```css
/* 手机端优化 */
@media (max-width: 768px) {
    .header {
        flex-direction: column;
        align-items: flex-start;
    }
    .header h1 {
        font-size: 1.5rem;
    }
    .status {
        font-size: 0.9rem;
    }
    .button-group {
        flex-wrap: wrap;
        gap: 0.5rem;
    }
    .control-btn {
        font-size: 0.9rem;
        padding: 0.5rem 0.8rem;
    }
}
```

#### 办公室监控页面
**文件位置**: `/opt/office-receiver/index.html`

**功能**：
- 显示办公室的温湿度
- 空调控制按钮
- 设备在线状态显示
- 移动端响应式布局

#### 电脑室监控页面
**文件位置**: `/opt/pcroom-receiver/index.html`

**功能**：
- 显示电脑室的温湿度
- **不包含**空调控制功能
- 设备在线状态显示
- 移动端响应式布局

**离线判断逻辑**：
```javascript
const offlineThreshold = 180;  // 电脑室180秒，其他90秒
const currentTime = Math.floor(Date.now() / 1000);
const isOffline = (data.timestamp === 0) ||
                   (currentTime - data.timestamp > offlineThreshold);
```

**数据获取**：
```javascript
async function fetchSensorData() {
    try {
        const response = await fetch('/data');
        const data = await response.json();
        updateDisplay(data);
    } catch (error) {
        console.error('获取数据失败:', error);
    }
}

// 每5秒刷新一次数据
setInterval(fetchSensorData, 5000);
```

**温湿度显示格式**：
```javascript
document.getElementById('temp').textContent = `${data.temperature}°C`;
document.getElementById('humidity').textContent = `${data.humidity}%`;
// 保留一位小数
```

---

## 三、系统运行流程

### 3.1 数据上传流程

```
ESP32设备
  ↓ (SHT30传感器读取)
  ↓ (每10/60秒)
  ↓ (HTTP POST, JSON格式)
Nginx反向代理 (端口7788/7789/7791)
  ↓ (端口映射)
Node.js接收服务 (端口3788/3789/3791)
  ↓ (解析并存储数据)
内存变量 (latestData)
  ↓ (HTTP GET)
前端页面
```

### 3.2 小智语音助手交互流程

```
用户唤醒小智
  ↓
MCP工具调用: sensor.read_temperature_humidity
  ↓
读取缓存的传感器值
  ↓
返回JSON: {"temperature": 25.6, "humidity": 65.3}
  ↓
小智语音播报
```

### 3.3 空调控制流程（客厅、办公室）

```
前端按钮点击
  ↓ (HTTP POST /ac)
Node.js服务接收
  ↓
发布MQTT消息: ac/livingroom/control 或 ac/office/control
  ↓
空调设备订阅Topic并接收消息
  ↓
执行控制指令
```

---

## 四、关键配置说明

### 4.1 上传间隔对比

| 设备 | 上传间隔 | 原因说明 |
|------|----------|----------|
| ESP32-main | 60秒 | 避免与小智唤醒词检测冲突，HTTP上传可能阻塞系统 |
| ESP32-office | 10秒 | 标准上传间隔，无需考虑语音唤醒冲突 |
| ESP32-pcroom | 10秒 | 标准上传间隔，无需考虑语音唤醒冲突 |

**技术原理**：
- 小智唤醒词检测需要持续监听麦克风数据
- HTTP上传操作需要占用网络资源，可能短暂阻塞
- 60秒间隔显著减少阻塞发生频率
- 温度变化缓慢，60秒采样率足够

### 4.2 离线阈值对比

| 服务 | 离线阈值 | 原因说明 |
|------|----------|----------|
| esp32-receiver | 90秒 | ESP32-main每60秒上传，90秒足够判断离线 |
| office-receiver | 90秒 | ESP32-office每10秒上传，90秒足够判断离线 |
| pcroom-receiver | 180秒 | ESP32-pcroom每10秒上传，180秒阈值避免频繁误判 |

**离线判断条件**：
1. 数据时间戳为0（首次加载）
2. 当前时间 - 上传时间 > 离线阈值

### 4.3 系统启动顺序

```bash
# 1. 启动MQTT Broker
systemctl start mosquitto

# 2. 启动Nginx
systemctl start nginx

# 3. 启动Node.js微服务
systemctl start esp32-receiver
systemctl start office-receiver
systemctl start pcroom-receiver

# 4. ESP32设备上电，自动连接WiFi并开始上传数据
```

---

## 五、常见问题排查

### 5.1 ESP32无法上传数据

**症状**：网页显示离线，ESP32串口无上传成功日志

**排查步骤**：
1. 检查ESP32 WiFi连接状态
2. 确认服务器IP和端口配置正确
3. 检查Nginx服务状态: `systemctl status nginx`
4. 检查防火墙规则，允许端口7788/7789/7791访问
5. 查看Node.js服务日志: `journalctl -u esp32-receiver -f`

### 5.2 小智唤醒灵敏度下降

**症状**：需要多次呼唤小智才能唤醒

**原因**：HTTP上传频繁导致系统阻塞

**解决**：
- 增加上传间隔至60秒（ESP32-main）
- 确保MCP工具返回缓存值而非重新读取传感器

### 5.3 网页显示"离线"但实际在线

**症状**：ESP32正常上传，网页却显示离线

**排查步骤**：
1. 检查Node.js服务是否正常运行
2. 确认离线阈值配置是否合理
3. 检查前端刷新逻辑，确认能正确获取数据
4. 查看浏览器控制台是否有网络错误

### 5.4 服务无法启动

**症状**：systemctl启动失败

**排查步骤**：
1. 检查端口占用: `lsof -i :3788`
2. 查看服务日志: `journalctl -u esp32-receiver -n 50`
3. 检查Node.js语法错误
4. 确认依赖包已安装: `npm install`

---

## 六、文件路径总览

### 6.1 ESP32固件代码（本地）
```
d:/ESP32/xiaozhi-esp32-main-SHT30/
├── main/
│   ├── sensor_upload.cc          # 传感器上传功能
│   ├── sensor_upload.h           # 上传类声明
│   └── boards/bread-compact-wifi-lcd/
│       └── compact_wifi_board_lcd.cc  # 主控制逻辑
└── CMakeLists.txt                # 构建配置
```

### 6.2 服务器端文件
```
/opt/
├── esp32-receiver/
│   ├── server.js                 # 客厅接收服务
│   ├── index.html                # 客厅监控页面（备份）
│   └── package.json
├── office-receiver/
│   ├── server.js                 # 办公室接收服务
│   ├── index.html                # 办公室监控页面
│   └── package.json
└── pcroom-receiver/
    ├── server.js                 # 电脑室接收服务
    ├── index.html                # 电脑室监控页面
    └── package.json

/var/www/html/
└── index.html                    # 主监控页面（客厅）

/etc/nginx/conf.d/
└── esp32-sensors.conf            # Nginx配置文件

/etc/systemd/system/
├── esp32-receiver.service        # 客厅服务systemd配置
├── office-receiver.service       # 办公室服务systemd配置
└── pcroom-receiver.service       # 电脑室服务systemd配置
```

### 6.3 日志文件
```
/var/log/
├── nginx/                        # Nginx日志
│   ├── access.log
│   └── error.log
└── mosquitto/                    # MQTT日志
    └── mosquitto.log

# Systemd服务日志通过journalctl查看
journalctl -u esp32-receiver -f
journalctl -u office-receiver -f
journalctl -u pcroom-receiver -f
```

---

## 七、系统维护建议

### 7.1 定期维护任务

| 任务 | 频率 | 命令 |
|------|------|------|
| 检查服务状态 | 每天 | `systemctl status esp32-receiver office-receiver pcroom-receiver` |
| 查看错误日志 | 每天 | `journalctl -p err -n 50` |
| 清理旧日志 | 每周 | `logrotate`自动处理 |
| 备份配置文件 | 每月 | 手动备份 `/etc/nginx` 和 `/opt/*` |

### 7.2 监控指标

- **服务可用性**: 各Node.js服务运行时间
- **数据上传率**: ESP32设备上传成功率
- **离线次数**: 各房间离线状态频率
- **MQTT消息量**: 空调控制消息统计

### 7.3 升级建议

1. **固件升级**：
   - ESP32固件需重新编译和烧录
   - 测试上传功能是否正常
   - 观察小智唤醒灵敏度

2. **服务升级**：
   - 备份当前服务文件
   - 停止服务: `systemctl stop esp32-receiver`
   - 替换server.js和package.json
   - 重启服务: `systemctl start esp32-receiver`

3. **前端升级**：
   - 直接替换index.html文件
   - 无需重启服务

---

## 八、技术栈总结

| 层级 | 技术栈 | 说明 |
|------|--------|------|
| 硬件 | ESP32-S3 | 微控制器，集成WiFi |
| 传感器 | SHT30 | I2C接口温湿度传感器 |
| 固件 | ESP-IDF v5.5.2 | Espressif官方开发框架 |
| 语言 | C++ | 固件开发语言 |
| Web服务器 | Nginx | 反向代理、静态文件服务 |
| 后端服务 | Node.js + Express | 数据接收和处理 |
| 消息队列 | MQTT (Mosquitto) | 设备间消息通信 |
| 前端 | HTML + CSS + JavaScript | 监控页面 |
| 服务管理 | systemd | Linux服务管理 |

---

## 九、版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| 1.0 | 2025-02-10 | 初始版本，完成三房间监控系统 |
| 1.1 | 2025-02-10 | 优化ESP32-main上传间隔至60秒 |
| 1.2 | 2025-02-10 | 修复MCP工具返回缓存值 |
| 1.3 | 2025-02-10 | 调整离线阈值（90s/180s） |
| 1.4 | 2025-02-10 | 移除电脑室空调控制按钮 |
| 1.5 | 2025-02-10 | 湿度显示保留一位小数 |
| 1.6 | 2025-02-10 | 添加移动端响应式布局 |
| 1.7 | 2025-02-10 | 修改"书房温度"为"客厅温度" |

---

## 十、参考资料

- ESP-IDF编程指南: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/
- Nginx官方文档: https://nginx.org/en/docs/
- Node.js官方文档: https://nodejs.org/docs/
- MQTT协议: https://mqtt.org/
- Express.js框架: https://expressjs.com/

---

**文档创建日期**: 2025年2月10日
**最后更新日期**: 2025年2月10日
**维护者**: 小智ESP32传感器系统团队
