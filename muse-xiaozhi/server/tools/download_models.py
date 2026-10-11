"""下载本机语音模型（识别 + 断句 + 朗读），放到 server/models/。总共约 300MB。

每个文件都核对 SHA-256，对不上只提醒、照样使用（上游有时会重新上传同名文件）。
先试 GitHub 官方地址，不通再试国内常用的 GitHub 加速地址。

用法（在 server 目录里）：
    .venv\\Scripts\\python tools\\download_models.py
"""

from __future__ import annotations

import hashlib
import os
import shutil
import sys
import tarfile
import time
import urllib.request

GH = "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
MIRRORS = [
    "",                                  # 直连 GitHub
    "https://ghfast.top/",               # 国内 GitHub 加速（第三方，未在开发环境验证）
    "https://gh-proxy.com/",             # 国内 GitHub 加速（第三方，未在开发环境验证）
]

# (下载路径, 文件 SHA-256, 大小, 解压后的目录名 或 None=单文件)
MODELS = [
    ("asr-models/silero_vad.onnx",
     "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6", 643854, None),
    ("asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2",
     "7d1efa2138a65b0b488df37f8b89e3d91a60676e416f515b952358d83dfd347e", 163002883,
     "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"),
    ("tts-models/matcha-icefall-zh-baker.tar.bz2",
     "fc41b5115c45ffab7f46e1a5a0fcefd15b2698e32ca5d080dde7a5e7caa2fe56", 75480421,
     "matcha-icefall-zh-baker"),
    ("vocoder-models/vocos-22khz-univ.onnx",
     "0574a135aa1db2de6e181050db2ec528496cacd4a4701fc5d7faf9f9804c0081", 53884024, None),
]

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS_DIR = os.path.join(HERE, "models")


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(rel: str, sha: str, size: int, dest: str) -> None:
    errors = []
    for m in MIRRORS:
        url = m + GH + rel
        part = dest + ".part"
        try:
            print(f"下载 {url}")
            t0 = time.time()
            with urllib.request.urlopen(url, timeout=60) as r, open(part, "wb") as f:
                got = 0
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
                    got += len(chunk)
                    print(f"\r  {got / 1e6:.0f}/{size / 1e6:.0f} MB", end="", flush=True)
            print(f"  用时 {time.time() - t0:.0f} 秒")
            if sha256_file(part) != sha:
                # 只提醒、不拦：上游有时会重新上传同名文件
                print("  提醒：校验值和记录的不一样（上游可能更新过），照样使用")
            os.replace(part, dest)
            return
        except Exception as e:  # 这个源不通就换下一个
            errors.append(f"{url}: {e}")
            if os.path.exists(part):
                os.remove(part)
    raise SystemExit("下载失败（所有地址都不行）：\n  " + "\n  ".join(errors))


def main() -> None:
    os.makedirs(MODELS_DIR, exist_ok=True)
    for rel, sha, size, folder in MODELS:
        name = rel.split("/")[-1]
        if folder is None:
            dest = os.path.join(MODELS_DIR, name)
            if os.path.isfile(dest):
                print("已有", name)
                continue
            fetch(rel, sha, size, dest)
        else:
            if os.path.isdir(os.path.join(MODELS_DIR, folder)):
                print("已有", folder)
                continue
            archive = os.path.join(MODELS_DIR, name)
            if not os.path.isfile(archive):
                fetch(rel, sha, size, archive)
            print("解压", name)
            with tarfile.open(archive, "r:bz2") as tar:
                tar.extractall(MODELS_DIR, filter="data")
            os.remove(archive)
            # 识别模型包里带的测试录音用不上，删掉省空间
            shutil.rmtree(os.path.join(MODELS_DIR, folder, "test_wavs"), ignore_errors=True)
    print("模型都准备好了：", MODELS_DIR)


if __name__ == "__main__":
    if sys.version_info < (3, 12):
        raise SystemExit("需要 Python 3.12")
    main()
