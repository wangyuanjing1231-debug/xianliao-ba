#include "pet_display.h"

#include "lvgl_theme.h"
#include "settings.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cmath>
#include <cstring>

#define TAG "PetDisplay"

namespace {
constexpr int64_t kThinkTimeoutUs = 90LL * 1000 * 1000;       // 思考状态最多保持 90 秒
constexpr int64_t kDoneTimeoutUs = 30LL * 60 * 1000 * 1000;   // "干完了"最多挂 30 分钟
constexpr int64_t kSadUs = 10LL * 1000 * 1000;                // 出错脸挂 10 秒
constexpr int kPetYOffset = 14;                               // 往下挪一点，给顶上状态栏留位置
constexpr int kAvatarPx = 3 * MUSE_PX_W;                      // 64 格放大 3 倍 = 192 像素
constexpr uint32_t kFrameMs = 80;                             // 每秒约 12 帧

bool IsSadEmotion(const char* e) {
    static const char* kSad[] = {"sad", "crying", "cancel", "cloud_off", "warning",
                                 "triangle_exclamation", "error", "angry"};
    for (auto s : kSad) {
        if (strcmp(e, s) == 0) return true;
    }
    return false;
}
}  // namespace

void PetDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    // 新设备默认深色主题（小怪兽在黑底上更好看）；用户改过就尊重用户的
    Settings settings("display", false);
    if (settings.GetString("theme").empty()) {
        auto* dark = LvglThemeManager::GetInstance().GetTheme("dark");
        if (dark != nullptr) {
            SetTheme(dark);
        }
    }

    DisplayLockGuard lock(this);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);  // 原来的表情不用了
    }

    buf_ = static_cast<uint16_t*>(heap_caps_malloc(kAvatarPx * kAvatarPx * sizeof(uint16_t),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf_ == nullptr) {
        ESP_LOGE(TAG, "no memory for avatar canvas");
        return;
    }
    memset(buf_, 0, kAvatarPx * kAvatarPx * sizeof(uint16_t));
    canvas_ = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas_, buf_, kAvatarPx, kAvatarPx, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(canvas_, LV_ALIGN_CENTER, 0, kPetYOffset);
    muse_pixel_set_size(kAvatarPx);

    // 状态栏、底部提示条要盖在小怪兽上面
    if (top_bar_) lv_obj_move_foreground(top_bar_);
    if (status_bar_) lv_obj_move_foreground(status_bar_);
    if (bottom_bar_) lv_obj_move_foreground(bottom_bar_);

    mode_since_us_ = esp_timer_get_time();
    timer_ = lv_timer_create(TimerCb, kFrameMs, this);
    ApplyLocked();
    DrawLocked();
}

void PetDisplay::DrawLocked() {
    if (canvas_ == nullptr) {
        return;
    }
    int64_t now = esp_timer_get_time();
    float t = now / 1e6f;
    // 板子拿不到实时音量，聆听和说话时用一个起伏的假音量，让小怪兽的嘴和光跟着动
    float level = 0.0f;
    if (mode_ == MUSE_MODE_LISTENING || mode_ == MUSE_MODE_SPEAKING) {
        level = 0.45f + 0.3f * sinf(t * 9.0f) + 0.15f * sinf(t * 23.0f);
        level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    }
    muse_pose_t pose = {
        .mode = mode_,
        .t = t,
        .mode_t = (now - mode_since_us_) / 1e6f,
        .level = level,
        .happy = happy_,
    };
    muse_pixel_render(&pose);
    muse_pixel_scale(buf_, kAvatarPx, 0, kAvatarPx - 1, 0, kAvatarPx - 1);
    lv_obj_invalidate(canvas_);
}

void PetDisplay::TimerCb(lv_timer_t* timer) {
    // LVGL 任务里执行，已经持有显示锁
    auto self = static_cast<PetDisplay*>(lv_timer_get_user_data(timer));
    // 定时检查过期的状态（思考太久、"干完了"挂太久、出错脸）
    int64_t now = esp_timer_get_time();
    bool expired = (self->thinking_ && now - self->think_since_us_ > kThinkTimeoutUs) ||
                   (self->done_ && now - self->done_since_us_ > kDoneTimeoutUs) ||
                   (self->sad_until_us_ != 0 && now > self->sad_until_us_);
    if (expired) {
        self->ApplyLocked();
    }
    self->DrawLocked();
}

void PetDisplay::ApplyLocked() {
    int64_t now = esp_timer_get_time();
    if (thinking_ && now - think_since_us_ > kThinkTimeoutUs) thinking_ = false;
    if (done_ && now - done_since_us_ > kDoneTimeoutUs) done_ = false;
    if (sad_until_us_ != 0 && now > sad_until_us_) sad_until_us_ = 0;

    muse_mode_t mode = MUSE_MODE_IDLE;
    float happy = 0.0f;
    switch (state_) {
        case kDeviceStateListening:
            mode = thinking_ ? MUSE_MODE_THINKING : MUSE_MODE_LISTENING;
            break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            mode = MUSE_MODE_SPEAKING;
            break;
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            mode = MUSE_MODE_BOOT;
            break;
        case kDeviceStateIdle:
            if (thinking_ || working_) {
                mode = MUSE_MODE_THINKING;   // 说完了等回话，或者后台在等 Muse
            } else if (done_) {
                happy = 1.0f;                // 后台的回答念完了，还没人理：开心脸
            }
            break;
        case kDeviceStateFatalError:
            mode = MUSE_MODE_ERROR;
            break;
        default:
            break;
    }
    if (sad_until_us_ != 0 && state_ != kDeviceStateSpeaking && state_ != kDeviceStateNotifying &&
        state_ != kDeviceStateListening) {
        mode = MUSE_MODE_ERROR;
    }
    if (mode != mode_) {
        mode_ = mode;
        mode_since_us_ = now;
    }
    happy_ = happy;
}

void PetDisplay::OnDeviceState(DeviceState state) {
    DisplayLockGuard lock(this);
    if (state == kDeviceStateListening && state_ != kDeviceStateListening) {
        thinking_ = false;  // 开始新一轮说话
        done_ = false;      // 有人来理它了，"干完了"就收起来
        sad_until_us_ = 0;
    }
    if (state == kDeviceStateSpeaking || state == kDeviceStateNotifying) {
        thinking_ = false;
    }
    state_ = state;
    ApplyLocked();
}

void PetDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    int64_t now = esp_timer_get_time();
    if (strcmp(emotion, "thinking") == 0) {
        thinking_ = true;
        think_since_us_ = now;
    } else if (strcmp(emotion, "pet_working") == 0) {
        working_ = true;
    } else if (strcmp(emotion, "pet_idle") == 0) {
        working_ = false;
    } else if (strcmp(emotion, "pet_done") == 0) {
        done_ = true;
        done_since_us_ = now;
    } else if (IsSadEmotion(emotion)) {
        sad_until_us_ = now + kSadUs;
    } else {
        return;  // 其他表情（neutral、happy 等）不影响小人
    }
    ApplyLocked();
}

void PetDisplay::SetChatMessage(const char* role, const char* content) {
    if (role != nullptr && strcmp(role, "system") == 0) {
        SpiLcdDisplay::SetChatMessage(role, content);  // 配网提示、出错提示照常显示
    } else {
        SpiLcdDisplay::SetChatMessage("system", "");   // 对话内容不上屏
    }
}
