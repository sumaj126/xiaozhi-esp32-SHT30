#ifndef SENSOR_UPLOAD_H
#define SENSOR_UPLOAD_H

#include <esp_http_client.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string>
#include <functional>

class SensorDataUploader {
public:
    // 上传回调函数：success(是否成功), message(消息)
    using UploadCallback = std::function<void(bool, const char*)>;

    SensorDataUploader();
    ~SensorDataUploader();

    // 配置上传参数
    void SetUploadUrl(const std::string& url);
    void SetApiKey(const std::string& api_key);
    void SetDeviceId(const std::string& device_id);
    void SetUploadInterval(int interval_seconds);

    // 启动/停止自动上传
    void Start();
    void Stop();

    // 手动上传一次温湿度数据
    // status: 设备状态（0=待机, 1=唤醒中, 2=录音中, 3=播放中, 4=配置中）
    bool UploadSensorData(float temperature, float humidity, int status = 0, UploadCallback callback = nullptr);

    // 设置上传回调
    void SetUploadCallback(UploadCallback callback);

private:
    std::string upload_url_;
    std::string api_key_;
    std::string device_id_;
    int upload_interval_seconds_;
    esp_timer_handle_t upload_timer_;
    bool is_running_;
    UploadCallback callback_;

    // 上一次的温湿度数据，用于去重
    float last_temperature_;
    float last_humidity_;
    bool has_last_data_;

    // 定时器回调
    static void TimerCallback(void* arg);

    // 执行HTTP POST请求
    bool PostData(const std::string& json_data, UploadCallback callback);

    // 构建JSON数据
    std::string BuildJson(float temperature, float humidity, int status = 0);
};

#endif // SENSOR_UPLOAD_H
