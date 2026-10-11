"""不接板子，直接测"发给 Muse → 等它写回来"这一圈。

用法（在 server 目录里）：.venv/bin/python tools/ask_muse.py "测试：请只回答两个字：收到"
会打印 Muse 写回的"要念的版本"和用时。手机 App 的副聊天里同时能看到完整回答。
"""

import asyncio
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import main  # noqa: E402
from xzserver.muse_client import MuseClient, MuseError  # noqa: E402


async def run(text: str) -> int:
    cfg = main.load_config(os.path.join(main.BASE, "config.yaml"))
    client = MuseClient(cfg["muse"])
    t0 = time.monotonic()
    try:
        async for reply in client.stream("test", text):
            print(f"Muse 写回（{time.monotonic() - t0:.1f} 秒）：{reply}")
            return 0
        print("Muse 写回了空文件")
        return 1
    except MuseError as e:
        print(f"失败（{time.monotonic() - t0:.1f} 秒）：{e}")
        return 1


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit('用法：python tools/ask_muse.py "要问的话"')
    raise SystemExit(asyncio.run(run(sys.argv[1])))
