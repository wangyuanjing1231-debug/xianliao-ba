#include "pet_display.h"

#include "lvgl_theme.h"
#include "settings.h"

#include <esp_log.h>
#include <cstring>

#define TAG "PetDisplay"

namespace {
constexpr int64_t kThinkTimeoutUs = 90LL * 1000 * 1000;       // 思考状态最多保持 90 秒
constexpr int64_t kDoneTimeoutUs = 30LL * 60 * 1000 * 1000;   // "干完了"最多挂 30 分钟
constexpr int64_t kSadUs = 10LL * 1000 * 1000;                // 出错脸挂 10 秒
constexpr int kPetYOffset = 14;                               // 往下挪一点，给顶上状态栏留位置

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

    // 新设备默认深色主题（像素小人在黑底上更清楚，科室里也不刺眼）；用户改过就尊重用户的
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
    pet_ = lv_image_create(lv_screen_active());
    lv_image_set_antialias(pet_, false);  // 放大时保持像素块，不糊
    lv_obj_align(pet_, LV_ALIGN_CENTER, 0, kPetYOffset);
    // 状态栏、底部提示条要盖在小人上面
    if (top_bar_) lv_obj_move_foreground(top_bar_);
    if (status_bar_) lv_obj_move_foreground(status_bar_);
    if (bottom_bar_) lv_obj_move_foreground(bottom_bar_);

    timer_ = lv_timer_create(TimerCb, 300, this);
    ShowPoseLocked(&pet_pose_idle);
}

void PetDisplay::ShowPoseLocked(const pet_pose_t* pose) {
    if (pet_ == nullptr || pose == nullptr || pose == pose_) {
        return;
    }
    pose_ = pose;
    frame_ = 0;
    lv_image_set_scale(pet_, 256 * pose->scale);
    ShowFrameLocked();
}

void PetDisplay::ShowFrameLocked() {
    if (pose_ == nullptr || pose_->count == 0) {
        return;
    }
    const lv_image_dsc_t* img = pose_->frames[frame_ % pose_->count];
    lv_image_set_src(pet_, img);
    lv_image_set_pivot(pet_, img->header.w / 2, img->header.h / 2);
    lv_obj_align(pet_, LV_ALIGN_CENTER, 0, kPetYOffset);
    if (timer_ != nullptr) {
        lv_timer_set_period(timer_, pose_->ms[frame_ % pose_->count]);
    }
}

void PetDisplay::TimerCb(lv_timer_t* timer) {
    // LVGL 任务里执行，已经持有显示锁
    auto self = static_cast<PetDisplay*>(lv_timer_get_user_data(timer));
    if (self->pose_ == nullptr) {
        return;
    }
    // 定时检查过期的状态（思考太久、"干完了"挂太久、出错脸）
    int64_t now = esp_timer_get_time();
    bool expired = (self->thinking_ && now - self->think_since_us_ > kThinkTimeoutUs) ||
                   (self->done_ && now - self->done_since_us_ > kDoneTimeoutUs) ||
                   (self->sad_until_us_ != 0 && now > self->sad_until_us_);
    if (expired) {
        self->ApplyLocked();
    }
    if (self->pose_->count > 1) {
        self->frame_ = (self->frame_ + 1) % self->pose_->count;
        self->ShowFrameLocked();
    }
}

void PetDisplay::ApplyLocked() {
    int64_t now = esp_timer_get_time();
    if (thinking_ && now - think_since_us_ > kThinkTimeoutUs) thinking_ = false;
    if (done_ && now - done_since_us_ > kDoneTimeoutUs) done_ = false;
    if (sad_until_us_ != 0 && now > sad_until_us_) sad_until_us_ = 0;

    const pet_pose_t* pose = &pet_pose_idle;
    switch (state_) {
        case kDeviceStateListening:
            pose = thinking_ ? &pet_pose_think : &pet_pose_listen;
            break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            pose = &pet_pose_speak;
            break;
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            pose = &pet_pose_think;
            break;
        case kDeviceStateIdle:
            if (thinking_) {
                pose = &pet_pose_think;      // 按键说完了，服务端还在想
            } else if (done_) {
                pose = &pet_pose_done;       // 后台的活办完了，还没人理
            } else if (working_) {
                pose = &pet_pose_work;       // 后台有活在办
            }
            break;
        case kDeviceStateFatalError:
            pose = &pet_pose_sad;
            break;
        default:
            break;
    }
    if (sad_until_us_ != 0 && state_ != kDeviceStateSpeaking && state_ != kDeviceStateNotifying &&
        state_ != kDeviceStateListening) {
        pose = &pet_pose_sad;
    }
    ShowPoseLocked(pose);
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
