// Muse 版小智的屏幕：中间是 Muse 的小怪兽（muse_pixel.c 实时画，放大 3 倍），按设备状态和服务端推来的状态换模样。
// 待机 / 聆听 / 思考 / 说话 / 干活中（用思考）/ 干完了（待机 + 开心）/ 出错。
#pragma once

#include "device_state.h"
#include "display/lcd_display.h"

extern "C" {
#include "muse_pixel.h"
}

#include <esp_timer.h>

class PetDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override;
    // 服务端用 {"type":"llm","emotion":"..."} 改状态：thinking / pet_working / pet_idle / pet_done
    void SetEmotion(const char* emotion) override;
    // 只显示系统提示（配网、出错），不在小屏上显示对话文字
    void SetChatMessage(const char* role, const char* content) override;
    // 设备状态变化时由板子调用（主任务里）
    void OnDeviceState(DeviceState state);

private:
    lv_obj_t* canvas_ = nullptr;
    uint16_t* buf_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    muse_mode_t mode_ = MUSE_MODE_BOOT;
    float happy_ = 0.0f;
    int64_t mode_since_us_ = 0;

    DeviceState state_ = kDeviceStateStarting;
    bool thinking_ = false;
    bool working_ = false;
    bool done_ = false;
    int64_t think_since_us_ = 0;
    int64_t done_since_us_ = 0;
    int64_t sad_until_us_ = 0;

    void ApplyLocked();
    void DrawLocked();
    static void TimerCb(lv_timer_t* timer);
};
