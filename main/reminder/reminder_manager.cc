#include "reminder_manager.h"
#include <esp_log.h>
#include <cstring>
#include <algorithm>

#define TAG "ReminderManager"
#define NVS_NAMESPACE "reminders"

bool Reminder::ShouldTrigger(time_t current_time) const {
    if (!enabled) {
        return false;
    }

    struct tm timeinfo;
    localtime_r(&current_time, &timeinfo);

    // 检查时间是否匹配
    if (timeinfo.tm_hour != hour || timeinfo.tm_min != minute || timeinfo.tm_sec != second) {
        return false;
    }

    // 根据重复模式检查
    switch (repeat_mode) {
        case ONCE:
            // 一次性提醒，需要检查是否已经触发过
            // 这里简化处理，假设每次启动后只检查当前时间
            return true;
        case DAILY:
            // 每天提醒，只要时间匹配就触发
            return true;
        case WEEKLY:
            // 每周提醒，需要检查是否是一周中的特定天
            // 这里简化处理，假设每周都会触发
            return true;
        case CUSTOM:
            // 自定义重复，检查位图
            int day_of_week = timeinfo.tm_wday; // 0=周日，1=周一，...，6=周六
            return (custom_repeat & (1 << day_of_week)) != 0;
        default:
            return false;
    }
}

std::string Reminder::ToString() const {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "[%s] %02d:%02d:%02d %s",
             enabled ? "✓" : "✗",
             hour, minute, second,
             message.c_str());
    return std::string(buffer);
}

ReminderManager::ReminderManager()
    : reminder_callback_(nullptr),
      check_timer_(nullptr),
      nvs_handle_(0),
      initialized_(false) {
}

ReminderManager::~ReminderManager() {
    Stop();
}

void ReminderManager::Initialize() {
    // 初始化NVS
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return;
    }

    LoadRemindersFromNVS();
    initialized_ = true;
    ESP_LOGI(TAG, "ReminderManager initialized with %d reminders", reminders_.size());
}

void ReminderManager::Start() {
    if (!initialized_) {
        ESP_LOGE(TAG, "ReminderManager not initialized");
        return;
    }

    // 创建定时器，每秒检查一次
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            ReminderManager* manager = static_cast<ReminderManager*>(arg);
            manager->CheckReminders();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reminder_check_timer",
        .skip_unhandled_events = true,
    };

    esp_err_t ret = esp_timer_create(&timer_args, &check_timer_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_timer_start_periodic(check_timer_, 1000000); // 1秒
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        esp_timer_delete(check_timer_);
        check_timer_ = nullptr;
        return;
    }

    ESP_LOGI(TAG, "ReminderManager started");
}

void ReminderManager::Stop() {
    if (check_timer_) {
        esp_timer_stop(check_timer_);
        esp_timer_delete(check_timer_);
        check_timer_ = nullptr;
    }

    if (nvs_handle_) {
        nvs_close(nvs_handle_);
        nvs_handle_ = 0;
    }

    ESP_LOGI(TAG, "ReminderManager stopped");
}

uint32_t ReminderManager::AddReminder(bool enabled, Reminder::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& message, uint8_t custom_repeat) {
    if (!initialized_) {
        ESP_LOGE(TAG, "ReminderManager not initialized");
        return 0;
    }

    // 生成唯一ID
    uint32_t id = 1;
    if (!reminders_.empty()) {
        id = reminders_.back().id + 1;
    }

    Reminder reminder(id, enabled, repeat_mode, hour, minute, second, message, custom_repeat);
    reminders_.push_back(reminder);
    SaveRemindersToNVS();

    ESP_LOGI(TAG, "Added reminder: %s", reminder.ToString().c_str());
    return id;
}

bool ReminderManager::UpdateReminder(uint32_t id, bool enabled, Reminder::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& message, uint8_t custom_repeat) {
    if (!initialized_) {
        ESP_LOGE(TAG, "ReminderManager not initialized");
        return false;
    }

    auto it = std::find_if(reminders_.begin(), reminders_.end(), [id](const Reminder& r) {
        return r.id == id;
    });

    if (it == reminders_.end()) {
        ESP_LOGE(TAG, "Reminder not found: %d", id);
        return false;
    }

    it->enabled = enabled;
    it->repeat_mode = repeat_mode;
    it->hour = hour;
    it->minute = minute;
    it->second = second;
    it->message = message;
    it->custom_repeat = custom_repeat;

    SaveRemindersToNVS();
    ESP_LOGI(TAG, "Updated reminder: %s", it->ToString().c_str());
    return true;
}

bool ReminderManager::DeleteReminder(uint32_t id) {
    if (!initialized_) {
        ESP_LOGE(TAG, "ReminderManager not initialized");
        return false;
    }

    auto it = std::find_if(reminders_.begin(), reminders_.end(), [id](const Reminder& r) {
        return r.id == id;
    });

    if (it == reminders_.end()) {
        ESP_LOGE(TAG, "Reminder not found: %d", id);
        return false;
    }

    ESP_LOGI(TAG, "Deleted reminder: %s", it->ToString().c_str());
    reminders_.erase(it);
    SaveRemindersToNVS();
    return true;
}

bool ReminderManager::ToggleReminder(uint32_t id) {
    if (!initialized_) {
        ESP_LOGE(TAG, "ReminderManager not initialized");
        return false;
    }

    auto it = std::find_if(reminders_.begin(), reminders_.end(), [id](const Reminder& r) {
        return r.id == id;
    });

    if (it == reminders_.end()) {
        ESP_LOGE(TAG, "Reminder not found: %d", id);
        return false;
    }

    it->enabled = !it->enabled;
    SaveRemindersToNVS();
    ESP_LOGI(TAG, "Toggled reminder: %s", it->ToString().c_str());
    return true;
}

std::vector<Reminder> ReminderManager::GetReminders() const {
    return reminders_;
}

Reminder ReminderManager::GetReminder(uint32_t id) const {
    auto it = std::find_if(reminders_.begin(), reminders_.end(), [id](const Reminder& r) {
        return r.id == id;
    });

    if (it != reminders_.end()) {
        return *it;
    }

    return Reminder();
}

void ReminderManager::SetReminderCallback(ReminderCallback callback) {
    reminder_callback_ = callback;
}

void ReminderManager::LoadRemindersFromNVS() {
    reminders_.clear();

    // 读取提醒数量
    uint32_t count = 0;
    esp_err_t ret = nvs_get_u32(nvs_handle_, "count", &count);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to read reminder count: %s", esp_err_to_name(ret));
        return;
    }

    if (count == 0) {
        ESP_LOGI(TAG, "No reminders found in NVS");
        return;
    }

    // 读取每个提醒
    for (uint32_t i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "reminder_%d", i);

        size_t size = 0;
        ret = nvs_get_blob(nvs_handle_, key, nullptr, &size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get reminder blob size: %s", esp_err_to_name(ret));
            continue;
        }

        std::vector<uint8_t> blob(size);
        ret = nvs_get_blob(nvs_handle_, key, blob.data(), &size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read reminder blob: %s", esp_err_to_name(ret));
            continue;
        }

        // 解析blob
        if (size >= sizeof(Reminder)) {
            Reminder reminder;
            memcpy(&reminder, blob.data(), sizeof(Reminder));
            
            // 读取消息
            if (size > sizeof(Reminder)) {
                reminder.message = std::string(reinterpret_cast<const char*>(blob.data() + sizeof(Reminder)), size - sizeof(Reminder));
            }

            reminders_.push_back(reminder);
        }
    }

    ESP_LOGI(TAG, "Loaded %d reminders from NVS", reminders_.size());
}

void ReminderManager::SaveRemindersToNVS() {
    // 清除旧数据
    nvs_erase_all(nvs_handle_);

    // 保存提醒数量
    esp_err_t ret = nvs_set_u32(nvs_handle_, "count", reminders_.size());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save reminder count: %s", esp_err_to_name(ret));
        return;
    }

    // 保存每个提醒
    for (uint32_t i = 0; i < reminders_.size(); i++) {
        const Reminder& reminder = reminders_[i];
        char key[32];
        snprintf(key, sizeof(key), "reminder_%d", i);

        // 准备blob
        size_t size = sizeof(Reminder) + reminder.message.size() + 1;
        std::vector<uint8_t> blob(size);
        memcpy(blob.data(), &reminder, sizeof(Reminder));
        memcpy(blob.data() + sizeof(Reminder), reminder.message.c_str(), reminder.message.size() + 1);

        ret = nvs_set_blob(nvs_handle_, key, blob.data(), size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save reminder: %s", esp_err_to_name(ret));
        }
    }

    // 提交更改
    ret = nvs_commit(nvs_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Saved %d reminders to NVS", reminders_.size());
}

void ReminderManager::CheckReminders() {
    time_t now = time(nullptr);

    for (const Reminder& reminder : reminders_) {
        if (reminder.ShouldTrigger(now)) {
            ESP_LOGI(TAG, "Reminder triggered: %s", reminder.ToString().c_str());
            if (reminder_callback_) {
                reminder_callback_(reminder);
            }
        }
    }
}

void ReminderManager::OnReminderTriggered(const Reminder& reminder) {
    if (reminder_callback_) {
        reminder_callback_(reminder);
    }
}
