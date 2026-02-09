#include "standby_screen.h"
#include "lvgl_theme.h"
#include "board.h"
#include <esp_log.h>
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include <cstring>
#include <cmath>

#define TAG "StandbyScreen"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

StandbyScreen::StandbyScreen(int width, int height)
    : width_(width)
    , height_(height)
    , is_visible_(false)
    , container_(nullptr)
    , date_label_(nullptr)
    , weekday_label_(nullptr)
    , time_label_(nullptr)
    , temperature_label_(nullptr)
    , humidity_label_(nullptr)
    , temp_icon_(nullptr)
    , humidity_icon_(nullptr)
    , divider_line_(nullptr)
    , update_timer_(nullptr)
    , current_temperature_(NAN)
    , current_humidity_(NAN) {

    // 不设置时区
    // 我们会在显示时手动调整时间

    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            StandbyScreen* screen = static_cast<StandbyScreen*>(arg);
            screen->UpdateTimerCallback();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "standby_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&timer_args, &update_timer_);
}

StandbyScreen::~StandbyScreen() {
    Hide();
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
        esp_timer_delete(update_timer_);
    }
}

void StandbyScreen::CreateUI() {
    if (container_ != nullptr) {
        return;
    }

    auto text_font = &font_puhui_20_4;
    auto icon_font = &font_awesome_20_4;

    auto screen = lv_screen_active();

    // 主容器 - 全屏背景
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_center(container_);

    // 第一行：日期（顶部居中）
    date_label_ = lv_label_create(container_);
    lv_label_set_text(date_label_, "");
    lv_obj_set_style_text_font(date_label_, text_font, 0);
    lv_obj_set_style_text_color(date_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(date_label_, LV_ALIGN_TOP_MID, 0, 16);

    // 第二行：星期（日期下方居中）
    weekday_label_ = lv_label_create(container_);
    lv_label_set_text(weekday_label_, "");
    lv_obj_set_style_text_font(weekday_label_, text_font, 0);
    lv_obj_set_style_text_color(weekday_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(weekday_label_, LV_TEXT_ALIGN_CENTER, 0);
    // 使用顶部居中对齐，Y轴位置基于date_label的底部
    lv_obj_align(weekday_label_, LV_ALIGN_TOP_MID, 0, 16 + 8 + text_font->line_height);

    // 第三行：时钟（屏幕中央偏左）
    time_label_ = lv_label_create(container_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_font(time_label_, text_font, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // 放大字体
    lv_obj_set_style_transform_scale(time_label_, 400, 0);
    
    // 定位到屏幕中央偏左
    lv_obj_align(time_label_, LV_ALIGN_CENTER, -20, 0);

    // 第三行：温湿度（底部左右）
    // 左边：温度
    temp_icon_ = lv_label_create(container_);
    lv_label_set_text(temp_icon_, LV_SYMBOL_IMAGE "°C");
    lv_obj_set_style_text_font(temp_icon_, icon_font, 0);
    lv_obj_set_style_text_color(temp_icon_, lv_color_hex(0xFF5722), 0);
    lv_obj_align(temp_icon_, LV_ALIGN_BOTTOM_LEFT, 16, -16);

    temperature_label_ = lv_label_create(container_);
    lv_label_set_text(temperature_label_, "--.-°C");
    lv_obj_set_style_text_font(temperature_label_, text_font, 0);
    lv_obj_set_style_text_color(temperature_label_, lv_color_white(), 0);
    lv_obj_align_to(temperature_label_, temp_icon_, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    // 右边：湿度
    humidity_icon_ = lv_label_create(container_);
    lv_label_set_text(humidity_icon_, LV_SYMBOL_SETTINGS "%");
    lv_obj_set_style_text_font(humidity_icon_, icon_font, 0);
    lv_obj_set_style_text_color(humidity_icon_, lv_color_hex(0x2196F3), 0);
    lv_obj_align(humidity_icon_, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    humidity_label_ = lv_label_create(container_);
    lv_label_set_text(humidity_label_, "--.-%");
    lv_obj_set_style_text_font(humidity_label_, text_font, 0);
    lv_obj_set_style_text_color(humidity_label_, lv_color_white(), 0);
    lv_obj_align_to(humidity_label_, humidity_icon_, LV_ALIGN_OUT_LEFT_MID, -8, 0);


}

void StandbyScreen::DestroyUI() {
    if (container_ != nullptr) {
        lv_obj_del(container_);
        container_ = nullptr;
        date_label_ = nullptr;
        weekday_label_ = nullptr;
        time_label_ = nullptr;
        temperature_label_ = nullptr;
        humidity_label_ = nullptr;
        temp_icon_ = nullptr;
        humidity_icon_ = nullptr;
        divider_line_ = nullptr;
    }
}

void StandbyScreen::Show() {
    if (is_visible_) {
        ESP_LOGI("StandbyScreen", "Already visible, skipping Show()");
        return;
    }

    ESP_LOGI("StandbyScreen", "Show() called, creating UI...");
    CreateUI();
    // 淡入动画，让待机界面出现更柔和
    if (container_) {
        lv_obj_set_style_opa(container_, LV_OPA_TRANSP, 0);
        lv_obj_fade_in(container_, 300, 0);
    }
    is_visible_ = true;

    // 启动定时更新（每秒更新时间）
    esp_err_t ret = esp_timer_start_periodic(update_timer_, 1000000); // 1秒
    if (ret != ESP_OK) {
        ESP_LOGE("StandbyScreen", "Failed to start timer: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI("StandbyScreen", "Timer started successfully");
    }
}

void StandbyScreen::Hide() {
    if (!is_visible_) {
        return;
    }

    esp_timer_stop(update_timer_);

    DestroyUI();
    is_visible_ = false;
}

void StandbyScreen::UpdateTime(const char* date, const char* weekday, const char* time) {
    cached_date_ = date;
    cached_weekday_ = weekday;
    cached_time_ = time;
    lv_async_call([](void* ctx) {
        StandbyScreen* screen = static_cast<StandbyScreen*>(ctx);
        screen->UpdateTimeUI();
    }, this);
}

void StandbyScreen::UpdateTimeUI() {
    if (!is_visible_ || date_label_ == nullptr) {
        ESP_LOGW("StandbyScreen", "UpdateTimeUI skipped - is_visible=%d, date_label=%p", is_visible_, (void*)date_label_);
        return;
    }

    ESP_LOGI("StandbyScreen", "Updating UI with time: date=%s, weekday=%s, time=%s",
             cached_date_.c_str(), cached_weekday_.c_str(), cached_time_.c_str());
    lv_label_set_text(date_label_, cached_date_.c_str());
    lv_label_set_text(weekday_label_, cached_weekday_.c_str());
    lv_label_set_text(time_label_, cached_time_.c_str());
}

void StandbyScreen::UpdateTemperatureHumidity(float temperature, float humidity) {
    current_temperature_ = temperature;
    current_humidity_ = humidity;

    if (!is_visible_ || temperature_label_ == nullptr) {
        return;
    }

    lv_async_call([](void* ctx) {
        StandbyScreen* screen = static_cast<StandbyScreen*>(ctx);
        screen->UpdateTemperatureHumidityUI();
    }, this);
}

void StandbyScreen::UpdateTemperatureHumidityUI() {
    if (!is_visible_ || temperature_label_ == nullptr) {
        ESP_LOGW("StandbyScreen", "UpdateTemperatureHumidityUI skipped - is_visible=%d, temperature_label=%p",
                 is_visible_, (void*)temperature_label_);
        return;
    }

    char temp_buf[32];
    char humi_buf[32];

    // 检查是否为NaN（未连接传感器）
    if (std::isnan(current_temperature_)) {
        snprintf(temp_buf, sizeof(temp_buf), "--.-°C");
        ESP_LOGW("StandbyScreen", "Temperature is NaN, displaying placeholder");
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", current_temperature_);

        // 根据温度设置颜色
        lv_color_t temp_color;
        if (current_temperature_ < 20.0f) {
            // 20度以下：蓝色
            temp_color = lv_color_hex(0x2196F3);
        } else if (current_temperature_ >= 20.0f && current_temperature_ <= 30.0f) {
            // 20-30度：浅黄色到深橙色渐变
            // 浅黄色(0xFFEB3B) -> 深橙色(0xFF5722)
            float ratio = (current_temperature_ - 20.0f) / 10.0f; // 0.0 ~ 1.0
            uint8_t r = 0xFF; // 红色保持255
            uint8_t g = 0xEB + (0x57 - 0xEB) * ratio; // 235 -> 87
            uint8_t b = 0x3B + (0x22 - 0x3B) * ratio; // 59 -> 34
            temp_color = lv_color_make(r, g, b);
        } else {
            // 30度以上：红色
            temp_color = lv_color_hex(0xF44336);
        }
        lv_obj_set_style_text_color(temperature_label_, temp_color, 0);
    }

    if (std::isnan(current_humidity_)) {
        snprintf(humi_buf, sizeof(humi_buf), "--.-%%");
        ESP_LOGW("StandbyScreen", "Humidity is NaN, displaying placeholder");
    } else {
        snprintf(humi_buf, sizeof(humi_buf), "%.1f%%", current_humidity_);
    }

    ESP_LOGI("StandbyScreen", "Updating temperature/humidity UI: %s %s", temp_buf, humi_buf);
    lv_label_set_text(temperature_label_, temp_buf);

    // 湿度始终用绿色显示
    if (!std::isnan(current_humidity_)) {
        lv_obj_set_style_text_color(humidity_label_, lv_color_hex(0x4CAF50), 0);
    }
    lv_label_set_text(humidity_label_, humi_buf);
}

void StandbyScreen::StartUpdate() {
    if (update_timer_ != nullptr) {
        esp_timer_start_periodic(update_timer_, 1000000); // 1秒
    }
}

void StandbyScreen::StopUpdate() {
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
    }
}

void StandbyScreen::UpdateTimerCallback() {
    ESP_LOGI("StandbyScreen", "Timer callback triggered");
    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    // 使用gmtime_r获取UTC时间
    gmtime_r(&now, &timeinfo);
    // 手动加8小时转换为东八区时间
    timeinfo.tm_hour += 8;
    // 处理跨日情况
    while (timeinfo.tm_hour >= 24) {
        timeinfo.tm_hour -= 24;
        timeinfo.tm_mday++;
    }
    // 重新计算星期
    time_t adjusted = mktime(&timeinfo);
    gmtime_r(&adjusted, &timeinfo);

    // 格式化日期
    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    // 星期数组
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo.tm_wday];

    // 格式化时间
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    ESP_LOGI("StandbyScreen", "Updating time: %s %s %s", date_buf, weekday, time_buf);
    UpdateTime(date_buf, weekday, time_buf);
}
