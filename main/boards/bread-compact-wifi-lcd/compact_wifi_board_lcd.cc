#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "sht30_sensor.h"
#include "sensor_upload.h"
#include "device_state.h"
#include "settings.h"

#include <esp_log.h>
#include <driver/uart.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_timer.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

class CompactWifiBoardLCD : public WifiBoard {
private:

    Button boot_button_;
    LcdDisplay* display_;
    SHT30Sensor* sht30_sensor_;
    SensorDataUploader* sensor_uploader_;

    // 上一次的设备状态，用于检测状态变化
    DeviceState last_device_state_;

    // 上次上传时间计数器
    int upload_counter_;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);

        // 初始化 SHT30 传感器 (UART_NUM_2, TX:GPIO17, RX:GPIO18, 9600 baud)
        sht30_sensor_ = new SHT30Sensor(UART_NUM_2, 17, 18, 9600);

        if (sht30_sensor_->IsInitialized()) {
            ESP_LOGI(TAG, "SHT30 sensor initialized");

            // 温度校准：显示值比实际高1度，设置偏移-1度
            sht30_sensor_->SetTemperatureOffset(-1.0f);

            // 注册 MCP 工具
            auto& mcp_server = McpServer::GetInstance();
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
                        ESP_LOGI(TAG, "MCP sensor read: temp=%.1f, hum=%.1f", temp, hum);
                    }
                    return result;
                });

            // 初始化温湿度数据上传器
            InitializeSensorUploader();
        } else {
            ESP_LOGW(TAG, "SHT30 sensor initialization failed");
        }

        // 启动设备状态监控定时器
        StartDeviceStateMonitor();
    }

    // 初始化温湿度数据上传器
    void InitializeSensorUploader() {
        sensor_uploader_ = new SensorDataUploader();

        // 硬编码上传参数
        std::string upload_url = "http://175.178.158.54:7791/update";
        std::string api_key = "";
        std::string device_id = "pcroom-esp32";

        sensor_uploader_->SetUploadUrl(upload_url);
        sensor_uploader_->SetApiKey(api_key);
        sensor_uploader_->SetDeviceId(device_id);

        // 设置上传回调
        sensor_uploader_->SetUploadCallback([this](bool success, const char* message) {
            if (success) {
                ESP_LOGI(TAG, "Sensor data uploaded: %s", message);
            } else {
                ESP_LOGW(TAG, "Sensor data upload failed: %s", message);
            }
        });

        ESP_LOGI(TAG, "Sensor data uploader initialized, URL: %s, Device ID: %s",
                 upload_url.c_str(), device_id.c_str());
    }

    // 设备状态监控定时器回调
    static void DeviceStateMonitorCallback(void* arg) {
        CompactWifiBoardLCD* board = static_cast<CompactWifiBoardLCD*>(arg);
        board->CheckDeviceState();
    }

    // 检查设备状态并切换界面
    void CheckDeviceState() {
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();

        ESP_LOGI(TAG, "CheckDeviceState: current=%d, last=%d", current_state, last_device_state_);

        // 检测状态变化
        if (current_state != last_device_state_) {
            ESP_LOGI(TAG, "Device state changed: %d -> %d", last_device_state_, current_state);

            switch (current_state) {
                case kDeviceStateIdle:
                    // 空闲状态，显示待机界面
                    if (display_) {
                        ESP_LOGI(TAG, "Idle state - showing standby screen");
                        display_->ShowStandbyScreen();
                    }
                    break;

                case kDeviceStateListening:
                case kDeviceStateSpeaking:
                    // 对话中（录音或播放），隐藏待机界面显示对话内容
                    if (display_) {
                        ESP_LOGI(TAG, "Listening/Speaking state - hiding standby screen");
                        display_->HideStandbyScreen();
                    }
                    break;

                case kDeviceStateWifiConfiguring:
                case kDeviceStateConnecting:
                case kDeviceStateActivating:
                case kDeviceStateUpgrading:
                case kDeviceStateAudioTesting:
                    // 配置或连接状态，隐藏待机界面显示主界面（状态栏、通知等）
                    if (display_) {
                        ESP_LOGI(TAG, "Configuring/Connecting state - hiding standby screen");
                        display_->HideStandbyScreen();
                    }
                    break;

                case kDeviceStateStarting:
                case kDeviceStateFatalError:
                    // 启动或错误状态，保持原有界面不变（可能是待机或对话界面）
                    // 不进行界面切换，避免界面闪烁
                    ESP_LOGI(TAG, "State %d - keeping current UI", current_state);
                    break;

                default:
                    // 未知状态，显示待机界面
                    if (display_) {
                        ESP_LOGI(TAG, "Unknown state %d - showing standby screen", current_state);
                        display_->ShowStandbyScreen();
                    }
                    break;
            }

            last_device_state_ = current_state;
        }

        // 更新温湿度数据（在待机模式下）
        if (display_) {
            if (sht30_sensor_ && sht30_sensor_->IsInitialized()) {
                float temp, humi;
                if (sht30_sensor_->ReadData(&temp, &humi)) {
                    ESP_LOGI(TAG, "SHT30 read successful: temp=%.1f°C, humi=%.1f%%", temp, humi);
                    display_->UpdateStandbyTemperatureHumidity(temp, humi);

                    // 上传温湿度数据到云服务器（每60秒上传一次）
                    upload_counter_++;
                    if (upload_counter_ >= 60) { // 60秒
                        ESP_LOGI(TAG, "Upload counter reached %d, attempting upload", upload_counter_);
                        if (sensor_uploader_) {
                            ESP_LOGI(TAG, "Calling UploadSensorData: temp=%.1f, humi=%.1f", temp, humi);
                            sensor_uploader_->UploadSensorData(temp, humi, nullptr);
                        } else {
                            ESP_LOGW(TAG, "Sensor uploader not available (sensor_uploader_=%p)", (void*)sensor_uploader_);
                        }
                        upload_counter_ = 0;
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to read SHT30 data");
                    display_->UpdateStandbyTemperatureHumidity(NAN, NAN);
                }
            } else {
                // SHT30未初始化，显示占位符
                ESP_LOGW(TAG, "SHT30 not available (sensor=%p), showing placeholder",
                         (void*)sht30_sensor_);
                display_->UpdateStandbyTemperatureHumidity(NAN, NAN);
            }
        }
    }

    // 启动设备状态监控
    void StartDeviceStateMonitor() {
        esp_timer_handle_t timer;
        esp_timer_create_args_t timer_args = {
            .callback = DeviceStateMonitorCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "state_monitor",
            .skip_unhandled_events = false,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));

        // 每秒检查一次状态
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000)); // 1秒
        ESP_LOGI(TAG, "Device state monitor started");
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        display_(nullptr),
        sht30_sensor_(nullptr),
        sensor_uploader_(nullptr),
        last_device_state_(kDeviceStateUnknown),
        upload_counter_(0) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }

        // 启动后立即显示待机界面
        if (display_) {
            ESP_LOGI(TAG, "Initial setup - showing standby screen");
            display_->ShowStandbyScreen();
        }
    }

    ~CompactWifiBoardLCD() {
        if (sht30_sensor_) {
            delete sht30_sensor_;
        }
        if (sensor_uploader_) {
            delete sensor_uploader_;
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
