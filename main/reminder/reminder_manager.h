#ifndef REMINDER_MANAGER_H
#define REMINDER_MANAGER_H

#include <vector>
#include <string>
#include <ctime>
#include <functional>
#include <esp_timer.h>
#include <nvs_flash.h>

class Reminder {
public:
    struct Time {
        int hour;
        int minute;
        int second;
    };

    enum RepeatMode {
        ONCE,
        DAILY,
        WEEKLY,
        CUSTOM
    };

    Reminder() : id(0), enabled(false), repeat_mode(ONCE), hour(0), minute(0), second(0), message(""), custom_repeat(0) {}
    Reminder(uint32_t id, bool enabled, RepeatMode repeat_mode, int hour, int minute, int second, const std::string& message, uint8_t custom_repeat)
        : id(id), enabled(enabled), repeat_mode(repeat_mode), hour(hour), minute(minute), second(second), message(message), custom_repeat(custom_repeat) {}

    uint32_t id;
    bool enabled;
    RepeatMode repeat_mode;
    int hour;
    int minute;
    int second;
    std::string message;
    uint8_t custom_repeat; // 位图，每一位代表一周中的一天，0=周日，1=周一，...，6=周六

    bool ShouldTrigger(time_t current_time) const;
    Time GetTime() const { return {hour, minute, second}; }
    std::string ToString() const;
};

class ReminderManager {
public:
    using ReminderCallback = std::function<void(const Reminder&)>;

    ReminderManager();
    ~ReminderManager();

    void Initialize();
    void Start();
    void Stop();

    uint32_t AddReminder(bool enabled, Reminder::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& message, uint8_t custom_repeat = 0);
    bool UpdateReminder(uint32_t id, bool enabled, Reminder::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& message, uint8_t custom_repeat = 0);
    bool DeleteReminder(uint32_t id);
    bool ToggleReminder(uint32_t id);

    std::vector<Reminder> GetReminders() const;
    Reminder GetReminder(uint32_t id) const;

    void SetReminderCallback(ReminderCallback callback);

private:
    std::vector<Reminder> reminders_;
    ReminderCallback reminder_callback_;
    esp_timer_handle_t check_timer_;
    nvs_handle_t nvs_handle_;
    bool initialized_;

    void LoadRemindersFromNVS();
    void SaveRemindersToNVS();
    void CheckReminders();
    void OnReminderTriggered(const Reminder& reminder);
};

#endif // REMINDER_MANAGER_H