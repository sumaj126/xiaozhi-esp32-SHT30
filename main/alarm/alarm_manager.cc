#include "alarm_manager.h"
#include <esp_log.h>
#include <cstring>
#include <sstream>

#define TAG "AlarmManager"
#define NVS_NAMESPACE "alarms"
#define MAX_ALARMS 10

bool Alarm::ShouldTrigger(time_t current_time) const {
    if (!enabled) {
        return false;
    }

    struct tm timeinfo;
    localtime_r(&current_time, &timeinfo);

    // 检查时间是否匹配
    if (timeinfo.tm_hour != hour || timeinfo.tm_min !=