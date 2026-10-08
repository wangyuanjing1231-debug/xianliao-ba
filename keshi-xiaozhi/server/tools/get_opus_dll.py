"""Windows 专用：准备 opus.dll（语音压缩库），放到 server/bin/opus.dll。

来源：PyPI 上 discord.py 2.7.1 安装包里自带的 libopus-0.x64.dll（Opus 官方代码编译，BSD 许可，
只依赖 Windows 自带的 KERNEL32.dll）。下载后核对 SHA-256，对不上就不用。

用法（在 server 目录里）：
    .venv\\Scripts\\python tools\\get_opus_dll.py
"""

from __future__ import annotations

import ctypes
import hashlib
import io
import os
import sys
import urllib.request
import zipfile

WHEEL_PATH = ("packages/f7/a7/17208c3b3f92319e7fad259f1c6d5a5baf8fd0654c54846ced329f83c3eb/"
              "discord_py-2.7.1-py3-none-any.whl")
WHEEL_SHA256 = "849dca2c63b171146f3a7f3f8acc04248098e9e6203412ce3cf2745f284f7439"
DLL_IN_WHEEL = "discord/bin/libopus-0.x64.dll"
DLL_SHA256 = "c84da836e8d92421ac305842cfe5a724898ed09d340d46b129e210bdc9448131"
LICENSE_IN_WHEEL = "discord/bin/COPYING"

SOURCES = [
    "https://files.pythonhosted.org/",            # PyPI 官方
    "https://pypi.tuna.tsinghua.edu.cn/",         # 清华镜像
    "https://mirrors.aliyun.com/pypi/",           # 阿里云镜像
]

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(HERE, "bin")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def download() -> bytes:
    errors = []
    for base in SOURCES:
        url = base + WHEEL_PATH
        try:
            print("下载", url)
            with urllib.request.urlopen(url, timeout=60) as r:
                data = r.read()
            if sha256(data) != WHEEL_SHA256:
                errors.append(f"{url}: 校验值不对")
                continue
            return data
        except Exception as e:  # 网络不通就换下一个源
            errors.append(f"{url}: {e}")
    raise SystemExit("所有下载源都失败了：\n  " + "\n  ".join(errors))


def main() -> None:
    if sys.maxsize <= 2**32:
        raise SystemExit("需要 64 位 Python")
    target = os.path.join(BIN, "opus.dll")
    os.makedirs(BIN, exist_ok=True)
    if os.path.isfile(target) and sha256(open(target, "rb").read()) == DLL_SHA256:
        print("opus.dll 已经在了：", target)
    else:
        z = zipfile.ZipFile(io.BytesIO(download()))
        dll = z.read(DLL_IN_WHEEL)
        if sha256(dll) != DLL_SHA256:
            raise SystemExit("安装包里的 opus.dll 校验值不对，停止")
        with open(target, "wb") as f:
            f.write(dll)
        with open(os.path.join(BIN, "opus-LICENSE.txt"), "wb") as f:
            f.write(z.read(LICENSE_IN_WHEEL))
        print("已写入", target)
    if os.name == "nt":
        lib = ctypes.CDLL(target)
        lib.opus_get_version_string.restype = ctypes.c_char_p
        print("加载成功：", lib.opus_get_version_string().decode())


if __name__ == "__main__":
    main()
