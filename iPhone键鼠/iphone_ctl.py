#!/usr/bin/env python3
"""
iPhone 投屏与 USB 键鼠控制命令行工具
对接 esp32-iphone-agent (v0.1.0) 服务端接口
"""

import sys
import os
import io
import time
import json
import urllib.request
import urllib.error
from PIL import Image

SERVER_URL = os.environ.get("IPHONE_AGENT_URL", "http://127.0.0.1:8890")
TIMEOUT = 5

def _get_json(path):
    url = f"{SERVER_URL}{path}"
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as e:
        if isinstance(e, urllib.error.HTTPError):
            body = e.read().decode("utf-8", errors="ignore")
            try:
                data = json.loads(body)
                raise RuntimeError(data.get("detail", body))
            except Exception:
                raise RuntimeError(f"HTTP {e.code}: {e.reason}")
        raise RuntimeError(f"无法连接到服务端 ({SERVER_URL}): {e.reason}")

def _post_json(path, data=None):
    url = f"{SERVER_URL}{path}"
    body = json.dumps(data or {}).encode("utf-8")
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="ignore")
        try:
            err_data = json.loads(err_body)
            msg = err_data.get("detail", err_body)
        except Exception:
            msg = f"HTTP {e.code}: {e.reason}"
        raise RuntimeError(msg)
    except urllib.error.URLError as e:
        raise RuntimeError(f"无法连接到服务端 ({SERVER_URL}): {e.reason}")

def _get_frame_bytes():
    url = f"{SERVER_URL}/api/frame.jpg"
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return resp.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise RuntimeError(f"获取画面失败 (HTTP {e.code})")
    except urllib.error.URLError as e:
        raise RuntimeError(f"无法连接到服务端 ({SERVER_URL}): {e.reason}")

def get_screen_size():
    data = _get_frame_bytes()
    if not data:
        return None
    try:
        im = Image.open(io.BytesIO(data))
        return im.size  # (width, height)
    except Exception:
        return None

def cmd_shot(args):
    """存当前投屏画面为 PNG 并打印路径，没在投屏就明说"""
    # 检查服务状态
    try:
        status = _get_json("/api/status")
    except Exception as e:
        print(f"错误: 服务端未正常运行 ({e})", file=sys.stderr)
        sys.exit(1)

    frame_data = _get_frame_bytes()
    if not frame_data or not status.get("screen", False):
        print("未在投屏。请在 iPhone 控制中心 -> 屏幕镜像 -> 选择 'iPhone Agent'（确保手机与电脑在同一 Wi-Fi）", file=sys.stderr)
        sys.exit(1)

    out_path = args[0] if len(args) > 0 else f"/tmp/iphone_shot_{int(time.time())}.png"
    out_path = os.path.abspath(out_path)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    im = Image.open(io.BytesIO(frame_data))
    im.save(out_path, format="PNG")
    print(out_path)

def _normalize_coords(x_val, y_val):
    x = float(x_val)
    y = float(y_val)
    if x <= 1.0 and y <= 1.0 and x >= 0.0 and y >= 0.0:
        return x, y

    # 大于 1 说明是截图像素坐标，尝试用当前画面尺寸归一化
    size = get_screen_size()
    if size:
        w, h = size
        nx = max(0.0, min(1.0, x / w))
        ny = max(0.0, min(1.0, y / h))
        return nx, ny
    else:
        raise RuntimeError("传入了绝对像素坐标，但当前无实时投屏画面，无法获取分辨率换算。请直接传入 0.0~1.0 的比例坐标，或开启投屏后再试。")

def _handle_error(action_name, e):
    err_msg = str(e)
    if "pointer not calibrated" in err_msg:
        print(f"错误: 指针未校准。请先在控制面板 http://192.168.6.170:8890/ 进行校准", file=sys.stderr)
    elif "board offline" in err_msg or "device" in err_msg:
        print(f"错误: ESP32 板子未连接到服务端，请确认板子通电并连上 Wi-Fi", file=sys.stderr)
    elif "asleep" in err_msg:
        print(f"错误: iPhone USB 已休眠，请解锁 iPhone 屏幕", file=sys.stderr)
    else:
        print(f"{action_name}失败: {err_msg}", file=sys.stderr)

def cmd_tap(args):
    """按截图坐标点一下，用项目自带的指针跟踪和校准"""
    if len(args) < 2:
        print("用法: iphone-tap <x> <y>", file=sys.stderr)
        print("示例 (像素坐标): iphone-tap 350 720", file=sys.stderr)
        print("示例 (比例坐标): iphone-tap 0.35 0.52", file=sys.stderr)
        sys.exit(2)

    try:
        nx, ny = _normalize_coords(args[0], args[1])
        res = _post_json("/api/tap", {"x": nx, "y": ny})
        print(f"点按成功: 目标 ({args[0]}, {args[1]}) -> 归一化比例 ({nx:.4f}, {ny:.4f})")
    except RuntimeError as e:
        _handle_error("点按", e)
        sys.exit(1)

def cmd_type(args):
    """打英文"""
    if len(args) < 1:
        print("用法: iphone-type <文字>", file=sys.stderr)
        print("示例: iphone-type \"Hello World\"", file=sys.stderr)
        sys.exit(2)

    text = " ".join(args)
    try:
        _post_json("/api/type", {"text": text})
        print(f"输入成功: \"{text}\"")
    except RuntimeError as e:
        _handle_error("输入", e)
        sys.exit(1)

def cmd_swipe(args):
    """滑动屏幕"""
    if len(args) < 4:
        print("用法: iphone-swipe <x1> <y1> <x2> <y2>", file=sys.stderr)
        print("示例: iphone-swipe 500 1500 500 500", file=sys.stderr)
        sys.exit(2)

    try:
        nx1, ny1 = _normalize_coords(args[0], args[1])
        nx2, ny2 = _normalize_coords(args[2], args[3])
        _post_json("/api/swipe", {"x1": nx1, "y1": ny1, "x2": nx2, "y2": ny2})
        print(f"滑动成功: ({args[0]}, {args[1]}) -> ({args[2]}, {args[3]}) [比例: ({nx1:.3f}, {ny1:.3f}) -> ({nx2:.3f}, {ny2:.3f})]")
    except RuntimeError as e:
        _handle_error("滑动", e)
        sys.exit(1)

def cmd_home(args):
    """回主屏幕"""
    try:
        _post_json("/api/rclick")  # 右键=回主屏（iPhone 辅助触控里给右键分配了主屏幕）
        print("回主屏幕成功")
    except RuntimeError as e:
        _handle_error("回主屏幕", e)
        sys.exit(1)

def cmd_back(args):
    """返回上一页"""
    try:
        _post_json("/api/back")
        print("返回成功")
    except RuntimeError as e:
        _handle_error("返回", e)
        sys.exit(1)

def cmd_key(args):
    """发送按键或快捷键 (如 enter, space, cmd+h, esc)"""
    if len(args) < 1:
        print("用法: iphone-key <按键>", file=sys.stderr)
        print("示例: iphone-key enter", file=sys.stderr)
        sys.exit(2)
    key_name = args[0]
    try:
        _post_json("/api/key", {"text": key_name})
        print(f"按键成功: {key_name}")
    except RuntimeError as e:
        print(f"按键失败: {e}", file=sys.stderr)
        sys.exit(1)

def cmd_status(args):
    """查看状态"""
    try:
        status = _get_json("/api/status")
        print(json.dumps(status, indent=2, ensure_ascii=False))
    except RuntimeError as e:
        print(f"获取状态失败: {e}", file=sys.stderr)
        sys.exit(1)

COMMANDS = {
    "shot": cmd_shot,
    "tap": cmd_tap,
    "type": cmd_type,
    "swipe": cmd_swipe,
    "home": cmd_home,
    "back": cmd_back,
    "key": cmd_key,
    "status": cmd_status,
}

def main():
    prog = os.path.basename(sys.argv[0])
    args = sys.argv[1:]

    # 如果通过别名/软链接调用，如 iphone-shot, iphone-tap 等
    if prog.startswith("iphone-"):
        sub = prog[len("iphone-"):]
        if sub in COMMANDS:
            COMMANDS[sub](args)
            return

    if not args:
        print(f"用法: {prog} <子命令> [参数...]")
        print("可用子命令:")
        print("  shot [路径]            保存当前投屏画面为 PNG 并打印路径")
        print("  tap <x> <y>            点按指定坐标（支持像素或 0~1 比例）")
        print("  type <文字>            输入英文文字")
        print("  swipe <x1> <y1> <x2> <y2> 滑动")
        print("  home                   回主屏幕")
        print("  back                   返回")
        print("  key <按键>             发送按键 (如 enter, esc)")
        print("  status                 查看当前连接与校准状态")
        sys.exit(2)

    sub = args[0]
    if sub in COMMANDS:
        COMMANDS[sub](args[1:])
    else:
        print(f"未知子命令: {sub}", file=sys.stderr)
        sys.exit(2)

if __name__ == "__main__":
    main()
