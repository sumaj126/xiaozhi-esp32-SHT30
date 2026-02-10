#include "sensor_upload.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <cmath>

#define TAG "SensorUpload"
#define DEFAULT_UPLOAD_INTERVAL 300 // 默认5分钟上传一次

SensorDataUploader::SensorDataUploader()
    : upload_interval_seconds_(DEFAULT_UPLOAD_INTERVAL)
    , upload_timer_(nullptr)
    , is_running_(false)
    , last_temperature_(0)
    , last_humidity_(0)
    , has_last_data_(false) {
}

SensorDataUploader::~SensorDataUploader() {
    Stop();
}

void SensorDataUploader::SetUploadUrl(const std::string& url) {
    upload_url_ = url;
    ESP_LOGI(TAG, "Upload URL set to: %s", url.c_str());
}

void SensorDataUploader::SetApiKey(const std::string& api_key) {
    api_key_ = api_key;
    ESP_LOGI(TAG, "API Key set");
}

void SensorDataUploader::SetDeviceId(const std::string& device_id) {
    device_id_ = device_id;
    ESP_LOGI(TAG, "Device ID set to: %s", device_id.c_str());
}

void SensorDataUploader::SetUploadInterval(int interval_seconds) {
    upload_interval_seconds_ = interval_seconds;
    ESP_LOGI(TAG, "Upload interval set to: %d seconds", interval_seconds);
}

void SensorDataUploader::SetUploadCallback(UploadCallback callback) {
    callback_ = callback;
}

bool SensorDataUploader::UploadSensorData(float temperature, float humidity, UploadCallback callback) {
    // 检查数据是否有效
    if (std::isnan(temperature) || std::isnan(humidity)) {
        ESP_LOGW(TAG, "Invalid sensor data: temp=%f, humi=%f", temperature, humidity);
        if (callback) {
            callback(false, "Invalid sensor data");
        }
        return false;
    }

    // 去重：如果数据和上次一样，跳过上传
    if (has_last_data_ &&
        std::abs(temperature - last_temperature_) < 0.1f &&
        std::abs(humidity - last_humidity_) < 1.0f) {
        ESP_LOGD(TAG, "Sensor data unchanged, skipping upload");
        if (callback) {
            callback(true, "Data unchanged");
        }
        return true;
    }

    // 构建JSON数据
    std::string json_data = BuildJson(temperature, humidity);

    // 上传数据
    bool success = PostData(json_data, callback);

    // 只有上传成功时才保存数据
    if (success) {
        last_temperature_ = temperature;
        last_humidity_ = humidity;
        has_last_data_ = true;
    }

    return success;
}

std::string SensorDataUploader::BuildJson(float temperature, float humidity) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return "";
    }

    // 添加设备ID
    if (!device_id_.empty()) {
        cJSON_AddStringToObject(root, "device_id", device_id_.c_str());
    }

    // 添加温湿度数据
    cJSON_AddNumberToObject(root, "temperature", temperature);
    cJSON_AddNumberToObject(root, "humidity", humidity);

    // 添加时间戳
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);

    // 转换为字符串
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result = json_str ? json_str : "";

    // 清理
    free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Built JSON: %s", result.c_str());
    return result;
}

bool SensorDataUploader::PostData(const std::string& json_data, UploadCallback callback) {
    if (upload_url_.empty()) {
        ESP_LOGE(TAG, "Upload URL not configured");
        if (callback) {
            callback(false, "Upload URL not configured");
        }
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = upload_url_.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 2000;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;

    // 设置API Key头部
    char auth_header[256];
    if (!api_key_.empty()) {
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key_.c_str());
        config.user_agent = auth_header;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 设置请求体
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!api_key_.empty()) {
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_http_client_set_post_field(client, json_data.c_str(), json_data.length());

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);

    bool success = (err == ESP_OK);
    int status_code = 0;

    if (success) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 status_code, esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // 清理
    esp_http_client_cleanup(client);

    // 调用回调
    if (callback) {
        char message[256];
        if (success) {
            snprintf(message, sizeof(message), "Upload success, HTTP %d", status_code);
            callback(true, message);
        } else {
            snprintf(message, sizeof(message), "Upload failed: %s", esp_err_to_name(err));
            callback(false, message);
        }
    }

    return success;
}

void SensorDataUploader::Start() {
    if (is_running_) {
        ESP_LOGW(TAG, "Uploader already running");
        return;
    }

    if (upload_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = TimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "sensor_upload",
            .skip_unhandled_events = false,
        };
        esp_timer_create(&timer_args, &upload_timer_);
    }

    esp_err_t ret = esp_timer_start_periodic(upload_timer_, upload_interval_seconds_ * 1000000);
    if (ret == ESP_OK) {
        is_running_ = true;
        ESP_LOGI(TAG, "Sensor data uploader started, interval: %d seconds", upload_interval_seconds_);
    } else {
        ESP_LOGE(TAG, "Failed to start upload timer: %s", esp_err_to_name(ret));
    }
}

void SensorDataUploader::Stop() {
    if (!is_running_) {
        return;
    }

    if (upload_timer_ != nullptr) {
        esp_timer_stop(upload_timer_);
        esp_timer_delete(upload_timer_);
        upload_timer_ = nullptr;
    }

    is_running_ = false;
    ESP_LOGI(TAG, "Sensor data uploader stopped");
}

void SensorDataUploader::TimerCallback(void* arg) {
    ESP_LOGW(TAG, "TimerCallback called - this should be implemented differently");
}
