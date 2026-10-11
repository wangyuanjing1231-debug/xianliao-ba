"""把一句话交给 Muse，再等 Muse 把"要念的版本"写回本机。

去程：musegadget send-user-msg（以这台 Ubuntu 的身份发进 Muse 的一个副聊天，手机 App 里能看到）。
回程：musegadget 本身拿不到 Muse 的回复正文（Muse 2026-10-11 查过源码）。所以每条消息都带一段说明，
请 Muse 回答后调用本设备的 file.write，把口语版写进收件箱 inbox/<编号>.txt；这里看到文件就拿去念。
用 file.write 不用 system.run：回复文字不会经过 shell，不怕引号、$ 之类的符号。
"""

from __future__ import annotations

import asyncio
import logging
import os
import time
import uuid
from typing import AsyncIterator

log = logging.getLogger("muse")


class MuseError(Exception):
    pass


class MuseClient:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.command = cfg.get("command") or "musegadget"
        self.session_id = (cfg.get("session_id") or "").strip()
        self.inbox = os.path.expanduser(cfg.get("inbox_dir") or "~/muse-xiaozhi/inbox")
        self.timeout = float(cfg.get("reply_timeout_seconds", 180))
        self.template = cfg.get("prompt_template") or "{text}\n\n回答后请用 file.write 把要念的话写入 {path}"
        os.makedirs(self.inbox, exist_ok=True)

    def _sweep(self):
        """清掉一小时前没人领的回复（超时以后 Muse 才写回来的那种）。"""
        cutoff = time.time() - 3600
        for name in os.listdir(self.inbox):
            p = os.path.join(self.inbox, name)
            try:
                if os.path.isfile(p) and os.path.getmtime(p) < cutoff:
                    os.remove(p)
            except OSError:
                pass

    async def _send(self, message: str):
        args = [self.command, "send-user-msg"]
        if self.session_id:
            args += ["--session-id", self.session_id]
        args.append(message)
        try:
            proc = await asyncio.create_subprocess_exec(
                *args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        except FileNotFoundError as e:
            raise MuseError(f"连不上 Muse：找不到 {self.command} 命令") from e
        try:
            out, err = await asyncio.wait_for(proc.communicate(), timeout=30)
        except asyncio.TimeoutError:
            proc.kill()
            raise MuseError("连不上 Muse：发送超时")
        if proc.returncode != 0:
            detail = (err or out).decode("utf-8", "ignore").strip()[:200]
            raise MuseError(f"连不上 Muse：{detail or '发送失败'}")

    async def _wait_reply(self, path: str) -> str:
        deadline = time.monotonic() + self.timeout
        last_size = -1
        while time.monotonic() < deadline:
            if os.path.isfile(path):
                size = os.path.getsize(path)
                if size > 0 and size == last_size:  # 大小两次一样，算写完了
                    with open(path, encoding="utf-8", errors="replace") as f:
                        text = f.read().strip()
                    os.remove(path)
                    return text
                last_size = size
            await asyncio.sleep(0.5)
        raise MuseError("Muse 没有回话")

    async def stream(self, user_key: str, text: str) -> AsyncIterator[str]:
        """接口和以前的 OpenClaw 一样（hub 不用改）；Muse 是整段写回来的，所以只吐一次。"""
        self._sweep()
        rid = uuid.uuid4().hex[:8]
        path = os.path.join(self.inbox, f"{rid}.txt")
        message = self.template.format(id=rid, text=text, path=path)
        log.info("-> Muse [%s] %s", rid, text)
        t0 = time.monotonic()
        await self._send(message)
        reply = await self._wait_reply(path)
        log.info("<- Muse [%s] %.1fs %s", rid, time.monotonic() - t0, reply[:60])
        if reply:
            yield reply
