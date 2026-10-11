"""把 frames/*.png + poses.json 转成固件里用的 C 文件（LVGL 图片，ARGB8888）。

用法：python png2lvgl.py <输出目录>
默认输出到 ../board/keshi-desk-1.54/（pet_frames.c / pet_frames.h）。
换素材：替换 frames/ 里的 png（可以是任意大小的像素图，透明底），在 poses.json 里写好
每个状态用哪几张、每张停多少毫秒、放大几倍（scale，整数，图会在屏幕上按最近邻放大），然后重跑。
屏幕 240x240，顶上约 30 像素是状态栏，所以 "图宽 × scale" 最好不超过 230、"图高 × scale" 不超过 200。
"""

import json
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
POSES = ["idle", "listen", "think", "speak", "work", "done", "sad"]


def cname(file: str) -> str:
    return "pet_" + re.sub(r"[^a-zA-Z0-9]", "_", os.path.splitext(file)[0])


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "board", "keshi-desk-1.54")
    poses = json.load(open(os.path.join(HERE, "poses.json"), encoding="utf-8"))
    missing = [p for p in POSES if p not in poses]
    if missing:
        raise SystemExit(f"poses.json 缺少这些状态：{missing}")

    files = []
    for p in POSES:
        for f in poses[p]["frames"]:
            if f not in files:
                files.append(f)

    c = ['// 自动生成，不要手改。来源：firmware/pixel_pet/frames/*.png + poses.json（png2lvgl.py）',
         '#include "pet_frames.h"', ""]
    total = 0
    for f in files:
        img = Image.open(os.path.join(HERE, "frames", f)).convert("RGBA")
        w, h = img.size
        n = cname(f)
        raw = img.tobytes()
        data = bytearray(raw)
        data[0::4], data[2::4] = raw[2::4], raw[0::4]  # LVGL ARGB8888 在内存里是 B,G,R,A
        total += len(data)
        rows = [", ".join(f"0x{v:02x}" for v in data[i:i + 32]) for i in range(0, len(data), 32)]
        c.append(f"static const uint8_t {n}_map[] = {{\n  " + ",\n  ".join(rows) + "\n};")
        c.append(f"static const lv_image_dsc_t {n} = {{\n"
                 f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .flags = 0,\n"
                 f"               .w = {w}, .h = {h}, .stride = {w * 4}}},\n"
                 f"    .data_size = sizeof({n}_map),\n"
                 f"    .data = {n}_map,\n}};\n")

    for p in POSES:
        spec = poses[p]
        frames = spec["frames"]
        ms = spec.get("ms") or [300] * len(frames)
        if len(ms) != len(frames):
            raise SystemExit(f"{p}: ms 的个数要和 frames 一样")
        scale = int(spec.get("scale", 4))
        c.append(f"static const lv_image_dsc_t* const {p}_frames[] = {{" + ", ".join("&" + cname(f) for f in frames) + "};")
        c.append(f"static const uint16_t {p}_ms[] = {{" + ", ".join(str(int(x)) for x in ms) + "};")
        c.append(f"const pet_pose_t pet_pose_{p} = {{{p}_frames, {p}_ms, {len(frames)}, {scale}}};\n")

    h = """// 自动生成，不要手改（png2lvgl.py）
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

""" + "\n".join(f"extern const pet_pose_t pet_pose_{p};" for p in POSES) + """

#ifdef __cplusplus
}
#endif
"""
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, "pet_frames.c"), "w", encoding="utf-8").write("\n".join(c))
    open(os.path.join(out_dir, "pet_frames.h"), "w", encoding="utf-8").write(h)
    print(f"{len(files)} frames, {total / 1024:.1f} KB pixel data -> {out_dir}")


if __name__ == "__main__":
    main()
