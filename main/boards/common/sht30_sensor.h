#ifndef SHT30_SENSOR_H
#define SHT30_SENSOR_H

#include <driver/uart.h>
#include <esp_log.h>
#include <cmath>
#include <string>

class SHT30Sensor {
private:
    bool initialized_;
    float temperature_;
    float humidity_;
    float temperature_offset_;  // 温度校准偏移量
    float humidity_offset_;     // 湿度校准偏移量
    uart_port_t uart_port_;
    int rx_buffer_size_;

    // 解析 SHT30 返回的数据格式: "R:039.2RH 023.3C"
    bool ParseData(const char* data, float* temperature, float* humidity) {
        // 数据格式: "R:039.2RH 023.3C"
        // 湿度: 039.2 (RH单位)
        // 温度: 023.3 (C单位)

        const char* rh_pos = strstr(data, "R:");
        if (!rh_pos) {
            ESP_LOGW("SHT30", "Invalid data format: missing 'R:'");
            return false;
        }

        // 提取湿度值
        float humidity_val = 0.0f;
        int rh_parsed = sscanf(rh_pos + 2, "%fRH", &humidity_val);
        if (rh_parsed != 1) {
            ESP_LOGW("SHT30", "Failed to parse humidity from: %s", rh_pos);
            return false;
        }

        // 提取温度值
        const char* temp_pos = strstr(data, "RH ");
        if (!temp_pos) {
            ESP_LOGW("SHT30", "Invalid data format: missing 'RH '");
            return false;
        }

        float temp_val = 0.0f;
        int temp_parsed = sscanf(temp_pos + 3, "%fC", &temp_val);
        if (temp_parsed != 1) {
            ESP_LOGW("SHT30", "Failed to parse temperature from: %s", temp_pos);
            return false;
        }

        *humidity = humidity_val + humidity_offset_;
        *temperature = temp_val + temperature_offset_;

        return true;
    }

public:
    SHT30Sensor(uart_port_t uart_port = UART_NUM_2, int tx_pin = 17, int rx_pin = 18, int baud_rate = 9600)
        : initialized_(false), temperature_(0.0f), humidity_(0.0f),
          temperature_offset_(0.0f), humidity_offset_(0.0f),
          uart_port_(uart_port), rx_buffer_size_(256) {

        // 配置 UART
        uart_config_t uart_config = {
            .baud_rate = baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        // 安装 UART 驱动
        esp_err_t ret = uart_driver_install(
            uart_port_,
            rx_buffer_size_ * 2,  // rx_buffer_size
            0,                     // tx_buffer_size
            0,                     // queue_size
            NULL,                  // queue_handle
            0                      // intr_alloc_flags
        );

        if (ret != ESP_OK) {
            ESP_LOGE("SHT30", "Failed to install UART driver: %s", esp_err_to_name(ret));
            return;
        }

        // 配置 UART 参数
        ret = uart_param_config(uart_port_, &uart_config);
        if (ret != ESP_OK) {
            ESP_LOGE("SHT30", "Failed to config UART: %s", esp_err_to_name(ret));
            uart_driver_delete(uart_port_);
            return;
        }

        // 设置 UART 引脚
        ret = uart_set_pin(
            uart_port_,
            tx_pin,  // TX pin
            rx_pin,  // RX pin
            UART_PIN_NO_CHANGE,  // RTS
            UART_PIN_NO_CHANGE   // CTS
        );

        if (ret != ESP_OK) {
            ESP_LOGE("SHT30", "Failed to set UART pins: %s", esp_err_to_name(ret));
            uart_driver_delete(uart_port_);
            return;
        }

        initialized_ = true;
        ESP_LOGI("SHT30", "SHT30 initialized on UART_NUM_%d (TX:GPIO%d, RX:GPIO%d, %d baud)",
                 uart_port, tx_pin, rx_pin, baud_rate);
    }

    ~SHT30Sensor() {
        if (initialized_) {
            uart_driver_delete(uart_port_);
        }
    }

    bool IsInitialized() const {
        return initialized_;
    }

    // 读取温度和湿度
    bool ReadData(float* temperature, float* humidity) {
        if (!initialized_) {
            return false;
        }

        // 尝试从 UART 读取数据（SHT30 自动上报）
        char buffer[rx_buffer_size_];
        int len = uart_read_bytes(uart_port_, (uint8_t*)buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));

        if (len <= 0) {
            // 没有新数据，返回缓存的值
            ESP_LOGV("SHT30", "No new data available, using cached values");
            *temperature = temperature_;
            *humidity = humidity_;
            return false;
        }

        buffer[len] = '\0';

        // 查找包含完整数据格式的行
        bool found = false;

        // 逐行检查，查找最后一个完整的数据行
        char* line_start = buffer;
        char* newline_pos;
        while ((newline_pos = strchr(line_start, '\n')) != NULL) {
            *newline_pos = '\0';  // 暂时截断字符串用于检查
            char* cr_pos = strchr(line_start, '\r');
            if (cr_pos) {
                *cr_pos = '\0';
            }

            // 检查是否包含完整的数据格式
            if (strstr(line_start, "R:") && strstr(line_start, "RH") && strstr(line_start, "C")) {
                // 尝试解析
                if (ParseData(line_start, temperature, humidity)) {
                    temperature_ = *temperature;
                    humidity_ = *humidity;
                    found = true;
                }
            }

            // 恢复换行符，继续查找
            *newline_pos = '\n';
            line_start = newline_pos + 1;
        }

        if (found) {
            ESP_LOGI("SHT30", "Temperature: %.2f°C, Humidity: %.2f%%", *temperature, *humidity);
            return true;
        } else {
            ESP_LOGW("SHT30", "No valid data found in buffer");
            // 返回缓存值
            *temperature = temperature_;
            *humidity = humidity_;
            return false;
        }
    }

    // 只读取温度
    float GetTemperature() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            return temp;
        }
        return temperature_;
    }

    // 只读取湿度
    float GetHumidity() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            return hum;
        }
        return humidity_;
    }

    // 获取 JSON 格式的数据
    std::string GetJsonData() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            char json[100];
            snprintf(json, sizeof(json),
                     "{\"temperature\": %.2f, \"humidity\": %.2f}",
                     temp, hum);
            return std::string(json);
        }
        return "{\"error\": \"Failed to read SHT30\"}";
    }

    // 设置温度校准偏移量（正数增加显示值，负数减少显示值）
    void SetTemperatureOffset(float offset) {
        temperature_offset_ = offset;
        ESP_LOGI("SHT30", "Temperature offset set to %.2f", offset);
    }

    // 设置湿度校准偏移量（正数增加显示值，负数减少显示值）
    void SetHumidityOffset(float offset) {
        humidity_offset_ = offset;
        ESP_LOGI("SHT30", "Humidity offset set to %.2f", offset);
    }

    // 获取温度校准偏移量
    float GetTemperatureOffset() const {
        return temperature_offset_;
    }

    // 获取湿度校准偏移量
    float GetHumidityOffset() const {
        return humidity_offset_;
    }

    // 校准温度（设置偏移量使显示值等于实际值）
    // actual_temp: 实际温度值（使用标准温度计测量）
    void CalibrateTemperature(float actual_temp) {
        float current_temp = GetTemperature();
        temperature_offset_ = actual_temp - current_temp;
        ESP_LOGI("SHT30", "Temperature calibrated: current=%.2f, actual=%.2f, offset=%.2f",
                 current_temp, actual_temp, temperature_offset_);
    }

    // 校准湿度（设置偏移量使显示值等于实际值）
    // actual_humidity: 实际湿度值（使用标准湿度计测量）
    void CalibrateHumidity(float actual_humidity) {
        float current_hum = GetHumidity();
        humidity_offset_ = actual_humidity - current_hum;
        ESP_LOGI("SHT30", "Humidity calibrated: current=%.2f, actual=%.2f, offset=%.2f",
                 current_hum, actual_humidity, humidity_offset_);
    }
};

#endif // SHT30_SENSOR_H
