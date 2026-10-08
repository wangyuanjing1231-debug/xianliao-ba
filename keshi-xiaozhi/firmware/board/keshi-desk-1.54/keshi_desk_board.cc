// 科室桌面版小智：硬件是微雪 ESP32-S3-Touch-LCD-1.54（不带电池版），从微雪官方板子代码改来。
// 和原版的区别：
//  - 屏幕换成像素小人（PetDisplay），对话文字不上屏，只显示系统提示；
//  - 不初始化触摸（只用实体按键），不做电池电量和省电关机（插 USB 常电）；
//  - 主按键：按一下 = 开始说话 / 说完了 / 打断；双击 = 开关屏幕背光；三击 = 重新配网；
//  - 音量键不变。
#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/led.h"
#include "backlight.h"
#include "assets/lang_config.h"
#include "pet_display.h"

#include <algorithm>
#include <string>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#define TAG "keshi_desk_1_54"

// 借用"指示灯"接口拿到设备状态变化，用来驱动像素小人（不改核心代码）
class PetStateLed : public Led {
public:
    explicit PetStateLed(PetDisplay* display) : display_(display) {}
    void OnStateChanged() override {
        if (display_ != nullptr) {
            display_->OnDeviceState(Application::GetInstance().GetDeviceState());
        }
    }

private:
    PetDisplay* display_;
};

class KeshiDeskBoard : public WifiBoard {
private:
    Button main_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    PetDisplay* display_ = nullptr;
    PetStateLed* led_ = nullptr;

    void KeepPowerOn() {
        // 带电池的版本靠这个脚自锁供电；不带电池版拉高也没有坏处
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << BATTERY_EN_PIN;
        gpio_config(&io_conf);
        gpio_set_level(BATTERY_EN_PIN, 1);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new PetDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                  DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                  DISPLAY_SWAP_XY);
        led_ = new PetStateLed(display_);
    }

    void InitializeButtons() {
        main_button_.OnClick([]() { Application::GetInstance().ToggleChatState(); });

        main_button_.OnDoubleClick([this]() {
            static uint8_t last = 0;
            auto backlight = GetBacklight();
            if (backlight->brightness() == 0) {
                backlight->SetBrightness(last > 0 ? last : 60, true);
            } else {
                last = backlight->brightness();
                backlight->SetBrightness(0);
            }
        });

        main_button_.OnMultipleClick([this]() { EnterWifiConfigMode(); }, 3);

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            int volume = std::min(codec->output_volume() + 10, 100);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
        });
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            int volume = std::max(codec->output_volume() - 10, 0);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
        });
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
                           "Reboot the device and enter WiFi configuration mode.\n"
                           "**CAUTION** You must ask the user to confirm this action.",
                           PropertyList(), [this](const PropertyList& properties) {
                               EnterWifiConfigMode();
                               return true;
                           });
    }

public:
    KeshiDeskBoard()
        : main_button_(PWR_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        KeepPowerOn();
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                         AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                                         AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
                                         AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, true);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Led* GetLed() override { return led_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(KeshiDeskBoard);
