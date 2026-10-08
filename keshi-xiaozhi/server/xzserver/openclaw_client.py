"""通过 OpenClaw 官方的 OpenAI 兼容接口（/v1/chat/completions）把一句话交给 OpenClaw。

OpenClaw 文档原话的意思：如果你的集成只是同一个网关的又一个"客户端入口"，优先用这个接口，
不用另写聊天渠道插件。小智就是这种情况。
"""

from __future__ import annotations

import json
import logging
from typing import AsyncIterator

import aiohttp

log = logging.getLogger("openclaw")


class OpenClawError(Exception):
    pass


class OpenClawClient:
    def __init__(self, cfg: dict, http: aiohttp.ClientSession):
        self.cfg = cfg
        self.http = http
        self.url = cfg.get("base_url", "http://127.0.0.1:18789/v1").rstrip("/") + "/chat/completions"

    async def stream(self, user_key: str, text: str) -> AsyncIterator[str]:
        """一边等一边吐字。user_key 相同的请求共用一个 OpenClaw 会话（有上下文）。"""
        headers = {"Content-Type": "application/json"}
        token = (self.cfg.get("token") or "").strip()
        if token:
            headers["Authorization"] = f"Bearer {token}"
        messages = []
        prompt = (self.cfg.get("voice_prompt") or "").strip()
        if prompt:
            messages.append({"role": "system", "content": prompt})
        messages.append({"role": "user", "content": text})
        body = {
            "model": self.cfg.get("model", "openclaw/default"),
            "stream": True,
            "user": user_key,
            "messages": messages,
        }
        timeout = aiohttp.ClientTimeout(total=None, sock_connect=10, sock_read=None)
        log.info("-> OpenClaw [%s] %s", user_key, text)
        try:
            async with self.http.post(self.url, json=body, headers=headers, timeout=timeout) as resp:
                if resp.status != 200:
                    detail = (await resp.text())[:300]
                    raise OpenClawError(f"HTTP {resp.status}: {detail}")
                async for raw in resp.content:
                    line = raw.decode("utf-8", "ignore").strip()
                    if not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    if data == "[DONE]":
                        break
                    try:
                        obj = json.loads(data)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(obj, dict) and obj.get("error"):
                        err = obj["error"]
                        msg = err.get("message") if isinstance(err, dict) else str(err)
                        raise OpenClawError(msg or "agent error")
                    for ch in obj.get("choices") or []:
                        delta = (ch.get("delta") or {}).get("content")
                        if delta:
                            yield delta
        except aiohttp.ClientConnectorError as e:
            raise OpenClawError(f"连不上 OpenClaw：{e}") from e
