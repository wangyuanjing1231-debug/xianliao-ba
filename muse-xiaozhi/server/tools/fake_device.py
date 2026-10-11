"""假小智：在没有板子时测试服务端。

用法：python tools/fake_device.py ws://127.0.0.1:8800 场景名
场景：
  ask "问题"           说一句话，听回答
  long "问题"          说一句要办很久的话：应该先听到"收到"，等一会儿再收到主动通知
  idle 秒数            什么都不做，等主动通知（配合 /api/push 测试）
  abort "问题"         问一句，回答念到一半按键打断
  silent              按了键但不说话
它会用本机朗读模型把问题念成语音，像真设备一样按 60ms 一帧发过去；
收到的语音会再用识别模型转回文字打印出来，方便核对。
"""

import asyncio
import json
import os
import sys
import time
import wave

import tempfile

import aiohttp
import numpy as np
import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from xzserver.audio import parse_ogg_packets, resample  # noqa: E402  （先导入它：Windows 上由它准备 opus.dll）
from xzserver.speech import Speech  # noqa: E402
import opuslib_next  # noqa: E402

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE_ID = os.environ.get("FAKE_DEVICE_ID", "aa:bb:cc:dd:ee:01")
OUT_DIR = os.environ.get("FAKE_OUT", os.path.join(tempfile.gettempdir(), "fake_device"))


def log(*a):
    print(f"[{time.strftime('%H:%M:%S')}]", *a, flush=True)


class FakeDevice:
    def __init__(self, base_url: str, speech: Speech, keepalive_fw: bool = True):
        self.base = base_url.rstrip("/")
        self.speech = speech
        self.keepalive_fw = keepalive_fw
        self.state = "idle"
        self.ws = None
        self.rx_pcm: list[bytes] = []
        self.dec = opuslib_next.Decoder(24000, 1)
        self.events: list[dict] = []
        self.heard: list[str] = []
        self.notifies: list[str] = []
        self.tts_done = asyncio.Event()

    async def connect(self, http: aiohttp.ClientSession):
        ota = self.base.replace("ws://", "http://") + "/xiaozhi/ota/"
        async with http.post(ota, json={"application": {"version": "fake"}}, headers={"Device-Id": DEVICE_ID}) as r:
            cfg = await r.json()
        log("OTA ->", cfg["websocket"])
        url = cfg["websocket"]["url"]
        headers = {"Device-Id": DEVICE_ID, "Client-Id": "fake-client", "Protocol-Version": "1"}
        if cfg["websocket"].get("token"):
            headers["Authorization"] = "Bearer " + cfg["websocket"]["token"]
        self.ws = await http.ws_connect(url, headers=headers)
        features = {"mcp": True}
        if self.keepalive_fw:
            features["keepalive"] = 1
        await self.ws.send_str(json.dumps({"type": "hello", "version": 1, "features": features, "transport": "websocket",
                                           "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}}))
        self.reader = asyncio.create_task(self._read(http))

    async def _read(self, http):
        async for msg in self.ws:
            if msg.type == aiohttp.WSMsgType.TEXT:
                m = json.loads(msg.data)
                self.events.append(m)
                t = m.get("type")
                if t == "keepalive":
                    log("  (心跳 keepalive)")
                    continue
                if t == "tts":
                    if m["state"] == "start":
                        self.state = "speaking"
                        self.rx_pcm = []
                        self.tts_done.clear()
                    elif m["state"] == "stop":
                        await self._finish_tts()
                        self.state = "idle"  # 新固件：说完回待机
                        self.tts_done.set()
                    elif m["state"] == "sentence_start":
                        log("  字幕:", m.get("text"))
                elif t == "stt":
                    log("  服务端听到:", m.get("text"))
                elif t == "llm":
                    log("  屏幕状态:", m.get("emotion"))
                elif t == "notify":
                    log("  收到 notify，去拉音频:", m["audio_url"])
                    asyncio.create_task(self._fetch_notify(http, m))
                else:
                    log("  其他消息:", m)
            elif msg.type == aiohttp.WSMsgType.BINARY:
                if self.state == "speaking":
                    self.rx_pcm.append(self.dec.decode(msg.data, 2880))
        log("连接断开")

    async def _transcribe(self, pcm: bytes, name: str) -> str:
        y = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768
        os.makedirs(OUT_DIR, exist_ok=True)
        with wave.open(os.path.join(OUT_DIR, name), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(24000); w.writeframes(pcm)
        return await self.speech.asr.transcribe(resample(y, 24000, 16000))

    async def _finish_tts(self):
        pcm = b"".join(self.rx_pcm)
        secs = len(pcm) / 2 / 24000
        if secs > 0.2:
            text = await self._transcribe(pcm, f"tts_{int(time.time())}.wav")
            self.heard.append(text)
            log(f"  >>> 设备念了 {secs:.1f} 秒：{text}")
        else:
            log("  >>> tts stop（没有声音）")

    async def _fetch_notify(self, http, m):
        if self.state != "idle":
            log("  设备不在待机，按固件规则忽略这条 notify")
            return
        self.state = "notifying"
        async with http.get(m["audio_url"]) as r:
            data = await r.read()
        info, packets = parse_ogg_packets(data)
        dec = opuslib_next.Decoder(info["rate"], 1)
        pcm = b"".join(dec.decode(p, 2880) for p in packets)
        text = await self._transcribe(pcm, f"notify_{int(time.time())}.wav")
        self.notifies.append(text)
        log(f"  >>> 主动通知（{len(pcm)/2/24000:.1f} 秒）：{text}")
        self.state = "idle"

    async def say(self, text: str, then_silence: float = 1.6):
        x = await self.speech.tts.synth(text)
        x16 = resample(x, 24000, 16000)
        enc = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_VOIP)
        frame = 960
        x16 = np.concatenate([np.zeros(4800, np.float32), x16, np.zeros(int(16000 * then_silence), np.float32)])
        x16 = np.concatenate([x16, np.zeros(frame - len(x16) % frame, np.float32)])
        pcm = (np.clip(x16, -1, 1) * 32767).astype(np.int16).tobytes()
        await self.ws.send_str(json.dumps({"type": "listen", "state": "start", "mode": "auto"}))
        self.state = "listening"
        log(f"按键说话：{text}")
        t0 = time.monotonic()
        for i in range(0, len(pcm), frame * 2):
            if self.state != "listening":
                break
            await self.ws.send_bytes(enc.encode(pcm[i:i + frame * 2], frame))
            await asyncio.sleep(max(0, t0 + (i // (frame * 2) + 1) * 0.06 - time.monotonic()))
        # 真设备在拿到 tts start 之前会一直发麦克风的声音
        silence = enc.encode(b"\x00" * frame * 2, frame)
        while self.state == "listening":
            await self.ws.send_bytes(silence)
            await asyncio.sleep(0.06)

    async def wait_tts(self, timeout=60):
        await asyncio.wait_for(self.tts_done.wait(), timeout)


async def main():
    url, scene = sys.argv[1], sys.argv[2]
    arg = sys.argv[3] if len(sys.argv) > 3 else ""
    cfg = yaml.safe_load(open(os.path.join(BASE, "config.example.yaml"), encoding="utf-8"))
    cfg["speech"]["threads"] = 2
    speech = Speech(cfg["speech"], BASE)
    async with aiohttp.ClientSession() as http:
        dev = FakeDevice(url, speech, keepalive_fw=(scene != "stock"))
        await dev.connect(http)
        await asyncio.sleep(0.5)
        if scene in ("ask", "stock"):
            await dev.say(arg or "今天天气怎么样")
            await dev.wait_tts(90)
        elif scene == "long":
            await dev.say(arg or "帮我慢慢整理一下这周的资料")
            await dev.wait_tts(90)
            log("等主动通知……")
            for _ in range(120):
                if dev.notifies:
                    break
                await asyncio.sleep(1)
        elif scene == "abort":
            await dev.say(arg or "今天天气怎么样")
            while dev.state != "speaking":
                await asyncio.sleep(0.05)
            await asyncio.sleep(0.8)
            log("按键打断！")
            await dev.ws.send_str(json.dumps({"type": "abort", "reason": "button"}))
            await dev.wait_tts(20)
        elif scene == "silent":
            await dev.ws.send_str(json.dumps({"type": "listen", "state": "start", "mode": "auto"}))
            dev.state = "listening"
            enc = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_VOIP)
            silence = enc.encode(b"\x00" * 1920, 960)
            t0 = time.monotonic()
            while dev.state == "listening" and time.monotonic() - t0 < 20:
                await dev.ws.send_bytes(silence)
                await asyncio.sleep(0.06)
            log("静默结束后设备状态:", dev.state, "用时", round(time.monotonic() - t0, 1))
        elif scene == "idle":
            want = int(os.environ.get("WANT", "1"))
            for _ in range(int(arg or 30)):
                await asyncio.sleep(1)
                if len(dev.notifies) >= want:
                    await asyncio.sleep(1)
                    break
        await asyncio.sleep(1)
        print(json.dumps({"heard": dev.heard, "notifies": dev.notifies,
                          "pixels": [e.get("emotion") for e in dev.events if e.get("type") == "llm"]},
                         ensure_ascii=False))
        await dev.ws.close()


if __name__ == "__main__":
    asyncio.run(main())
