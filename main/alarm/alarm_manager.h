#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <vector>
#include <string>
#include <ctime>
#include <functional>
#include <esp_timer.h>
#include <nvs_flash.h>

class Alarm {
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

    Alarm() : id(0), enabled(false), repeat_mode(ONCE), hour(0), minute(0), second(0), label(""), custom_repeat(0) {}
    Alarm(uint32_t id, bool enabled, RepeatMode repeat_mode, int hour, int minute, int second, const std::string& label, uint8_t custom_repeat)
        : id(id), enabled(enabled), repeat_mode(repeat_mode), hour(hour), minute(minute), second(second), label(label), custom_repeat(custom_repeat) {}

    uint32_t id;
    bool enabled;
    RepeatMode repeat_mode;
    int hour;
    int minute;
    int second;
    std::string label;
    uint8_t custom_repeat; // 位图，每一位代表一周中的一天，0=周日，1=周一，...，6=周六

    bool ShouldTrigger(time_t current_time) const;
    Time GetTime() const { return {hour, minute, second}; }
    std::string ToString() const;
};

class AlarmManager {
public:
    using AlarmCallback = std::function<void(const Alarm&)>;

    AlarmManager();
    ~AlarmManager();

    void Initialize();
    void Start();
    void Stop();

    uint32_t AddAlarm(bool enabled, Alarm::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& label, uint8_t custom_repeat = 0);
    bool UpdateAlarm(uint32_t id, bool enabled, Alarm::RepeatMode repeat_mode, int hour, int minute, int second, const std::string& label, uint8_t custom_repeat = 0);
    bool DeleteAlarm(uint32_t id);
    bool ToggleAlarm(uint32_t id);

    std::vector<Alarm> GetAlarms() const;
    Alarm GetAlarm(uint32_t id) const;

    void SetAlarmCallback(AlarmCallback callback);

private:
    std::vector<Alarm> alarms_;
    AlarmCallback alarm_callback_;
    esp_timer_handle_t check_timer_;
    nvs_handle_t nvs_handle_;
    bool initialized_;

    void LoadAlarmsFromNVS();
    void SaveAlarmsToNVS();
    void CheckAlarms();
    void OnAlarmTriggered(const Alarm& alarm);
};

#endif // ALARM_MANAGER_H