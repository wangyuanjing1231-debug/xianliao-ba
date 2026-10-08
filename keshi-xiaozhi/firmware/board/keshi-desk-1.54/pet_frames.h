// 自动生成，不要手改（png2lvgl.py）
#pragma once
#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const lv_image_dsc_t* const* frames;  // 帧
    const uint16_t* ms;                   // 每帧停留毫秒
    uint8_t count;                        // 帧数
    uint8_t scale;                        // 屏幕上放大倍数（最近邻）
} pet_pose_t;

extern const pet_pose_t pet_pose_idle;
extern const pet_pose_t pet_pose_listen;
extern const pet_pose_t pet_pose_think;
extern const pet_pose_t pet_pose_speak;
extern const pet_pose_t pet_pose_work;
extern const pet_pose_t pet_pose_done;
extern const pet_pose_t pet_pose_sad;

#ifdef __cplusplus
}
#endif
