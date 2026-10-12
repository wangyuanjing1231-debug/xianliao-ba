"""一台小智设备的一条 WebSocket 连接：收音、断句、识别、交给 Muse、朗读回去。"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
import time
import uuid

import numpy as np
from aiohttp import WSMsgType, web

from .audio import DEVICE_IN_RATE, DownlinkEncoder, FRAME_MS, UplinkDecoder
from .textclean import SentenceAssembler, echo_head

log = logging.getLogger("session")

PREBUFFER_SECONDS = 0.3   # 最多比设备播放进度超前这么多秒发包（设备缓冲只有约 1.2 秒）


class Speaker:
    """一次 "tts start ... tts stop" 的朗读过程。"""

    def __init__(self, sess: "DeviceSession"):
        self.sess = sess
        self.enc = DownlinkEncoder()
        self.started = False
        self.t0 = 0.0
        self.sent = 0.0  # 已经发出去的音频秒数

    async def start(self):
        if not self.started:
            self.started = True
            self.sess.state = "speaking"
            await self.sess.send_json({"type": "tts", "state": "start"})

    async def say(self, display: str, spoken: str):
        await self.start()
        x = await self.sess.hub.speech.tts.synth(spoken)
        packets = self.enc.encode(x)
        if not packets:
            return
        await self.sess.send_json({"type": "tts", "state": "sentence_start", "text": display.strip()})
        frame = FRAME_MS / 1000
        for pkt in packets:
            now = time.monotonic()
            if self.t0 == 0.0:
                self.t0 = now
            ahead = self.sent - (now - self.t0)
            if ahead < 0:  # 合成慢了，设备已经播完在等：从现在重新计时
                self.t0 = now - self.sent
                ahead = 0
            if ahead > PREBUFFER_SECONDS:
                await asyncio.sleep(ahead - PREBUFFER_SECONDS)
            await self.sess.send_audio(pkt)
            self.sent += frame

    async def finish(self):
        if not self.started:
            return
        if self.t0:
            remain = self.sent - (time.monotonic() - self.t0)
            if remain > 0:
                await asyncio.sleep(remain + 0.15)
        await self.sess.send_json({"type": "tts", "state": "stop"})
        self.sess.state = "idle"


class DeviceSession:
    def __init__(self, hub, ws: web.WebSocketResponse, request: web.Request):
        self.hub = hub
        self.ws = ws
        h = request.headers
        self.device_id = (h.get("Device-Id") or request.query.get("device-id") or "unknown").lower()
        self.client_id = h.get("Client-Id") or request.query.get("client-id") or ""
        try:
            self.version = int(h.get("Protocol-Version") or 1)
        except ValueError:
            self.version = 1
        # 通知音频的下载地址。设备连 WebSocket 时 Host 头里可能不带端口号（2026-10-11 实测），
        # 不补上的话设备会去 80 端口下载、一直拿不到，所以和 OTA 下发地址用同一套规则
        srv = hub.cfg["server"]
        self.host = srv.get("public_host") or request.host
        if ":" not in self.host:
            self.host = f"{self.host}:{srv.get('port', 8800)}"
        self.session_id = uuid.uuid4().hex
        self.ready = False
        self.closed = False
        self.draining = False
        self.state = "connected"  # connected / idle / listening / processing / speaking / notifying
        self.features: dict = {}
        self.listen_mode = "auto"
        self.vad = hub.speech.new_vad()
        self.decoder = UplinkDecoder()
        self.audio: list[np.ndarray] = []
        self.listen_started = 0.0
        self.turn: asyncio.Task | None = None
        self.job = None
        self.speaker: Speaker | None = None
        self._send_lock = asyncio.Lock()
        self._watch: asyncio.Task | None = None
        self.cfg = hub.cfg

    @property
    def supports_keepalive(self) -> bool:
        return bool(self.features.get("keepalive"))

    # ---------------- 发送 ----------------
    async def send_json(self, obj: dict):
        if self.closed:
            return
        obj.setdefault("session_id", self.session_id)
        async with self._send_lock:
            await self.ws.send_str(json.dumps(obj, ensure_ascii=False))

    async def send_audio(self, payload: bytes):
        if self.closed:
            return
        if self.version == 2:
            data = struct.pack("!HHIII", 2, 0, 0, 0, len(payload)) + payload
        elif self.version == 3:
            data = struct.pack("!BBH", 0, 0, len(payload)) + payload
        else:
            data = payload
        async with self._send_lock:
            await self.ws.send_bytes(data)

    async def set_pixel(self, emotion: str):
        """改屏幕上像素小人的状态（科室版固件认识 pet_* 这几个名字，旧固件会忽略）。"""
        try:
            await self.send_json({"type": "llm", "text": "", "emotion": emotion})
        except Exception:
            pass

    async def close(self, reason: str = ""):
        if not self.closed:
            log.info("[%s] close: %s", self.device_id, reason)
            self.closed = True
            await self.ws.close()

    # ---------------- 主循环 ----------------
    async def run(self):
        log.info("[%s] connected v%d from %s", self.device_id, self.version, self.host)
        self.hub.register(self)
        try:
            async for msg in self.ws:
                if msg.type == WSMsgType.TEXT:
                    await self._on_text(msg.data)
                elif msg.type == WSMsgType.BINARY:
                    await self._on_audio(msg.data)
                elif msg.type == WSMsgType.ERROR:
                    break
        finally:
            self.closed = True
            self.hub.unregister(self)
            if self._watch:
                self._watch.cancel()
            if self.turn and not self.turn.done():
                self.turn.cancel()
            if self.job and not self.job.finished and not self.job.background:
                # 设备掉线了，但事情已经在办：转后台，等它连回来再告诉它
                await self.hub.to_background(self.job, len(self.job.full_text))
            log.info("[%s] disconnected", self.device_id)

    async def _on_text(self, data: str):
        try:
            msg = json.loads(data)
        except json.JSONDecodeError:
            return
        t = msg.get("type")
        if t == "hello":
            self.features = msg.get("features") or {}
            await self.send_json({
                "type": "hello", "transport": "websocket",
                "audio_params": {"format": "opus", "sample_rate": 24000, "channels": 1,
                                 "frame_duration": FRAME_MS},
            })
            self.ready = True
            if self.state == "connected":
                self.state = "idle"
            log.info("[%s] hello features=%s", self.device_id, self.features)
            await self.hub.on_ready(self)
        elif t == "listen":
            st = msg.get("state")
            if msg.get("mode"):
                self.listen_mode = msg["mode"]
            if st == "start":
                await self._start_listening()
            elif st == "stop":
                await self._end_utterance("device-stop")
            elif st == "detect":
                log.info("[%s] wake word: %s", self.device_id, msg.get("text"))
        elif t == "abort":
            log.info("[%s] abort (%s)", self.device_id, msg.get("reason", "button"))
            await self._interrupt(send_stop=True)
        elif t == "mcp":
            pass  # 设备自带的 MCP 工具暂不用
        else:
            log.debug("[%s] ignore %s", self.device_id, t)

    # ---------------- 收音和断句 ----------------
    async def _interrupt(self, send_stop: bool):
        """打断：停掉正在念的、取消还没转后台的活。"""
        if self.turn and not self.turn.done() and self.turn is not asyncio.current_task():
            self.turn.cancel()
            try:
                await self.turn
            except (asyncio.CancelledError, Exception):
                pass
        if self.job:
            self.hub.cancel_job(self.job)
            self.job = None
        was_speaking = self.speaker is not None and self.speaker.started
        self.speaker = None
        if send_stop and (was_speaking or self.state in ("speaking", "processing")):
            await self.send_json({"type": "tts", "state": "stop"})
        if self.state != "listening":
            self.state = "idle"

    async def _start_listening(self):
        if self.state in ("processing", "speaking"):
            await self._interrupt(send_stop=False)
        self.vad.reset()
        self.audio = []
        self.state = "listening"
        self.listen_started = time.monotonic()
        if self._watch:
            self._watch.cancel()
        self._watch = asyncio.create_task(self._listen_watchdog())

    async def _listen_watchdog(self):
        """一直没人说话就收工；说太久也强制收尾。"""
        vc = self.cfg["speech"]["vad"]
        no_speech = float(vc.get("no_speech_timeout", 8))
        max_len = float(vc.get("max_utterance_seconds", 30)) + 2
        try:
            while self.state == "listening":
                await asyncio.sleep(0.25)
                el = time.monotonic() - self.listen_started
                if not self.vad.speech_seen and el > no_speech:
                    log.info("[%s] no speech in %.0fs", self.device_id, no_speech)
                    await self._end_silently()
                    return
                if el > max_len:
                    await self._end_utterance("max-length")
                    return
        except asyncio.CancelledError:
            pass

    async def _end_silently(self):
        """没听到人声：让设备回到待机。新固件发一对 tts start/stop 就回去；旧固件只能断开。"""
        self.state = "idle"
        if self.supports_keepalive:
            await self.send_json({"type": "tts", "state": "start"})
            await self.send_json({"type": "tts", "state": "stop"})
        else:
            await self.close("no speech")

    async def _on_audio(self, data: bytes):
        if self.state != "listening":
            return
        if self.version == 2 and len(data) >= 16:
            size = struct.unpack("!I", data[12:16])[0]
            data = data[16:16 + size]
        elif self.version == 3 and len(data) >= 4:
            size = struct.unpack("!H", data[2:4])[0]
            data = data[4:4 + size]
        if not data:
            return
        try:
            pcm = self.decoder.decode(data)
        except Exception as e:
            log.debug("opus decode error: %s", e)
            return
        self.audio.append(pcm)
        if self.listen_mode == "manual":
            return  # 手动模式：等设备说"停"
        seg = self.vad.feed(pcm)
        if seg is not None:
            await self._end_utterance("vad", seg)

    async def _end_utterance(self, reason: str, seg: np.ndarray | None = None):
        if self.state != "listening":
            return
        if self._watch and self._watch is not asyncio.current_task():
            self._watch.cancel()
        self.state = "processing"
        if seg is None:
            seg = self.vad.flush()
        if seg is None or len(seg) < DEVICE_IN_RATE * 0.3:
            seg = np.concatenate(self.audio) if self.audio else np.zeros(0, np.float32)
        self.audio = []
        log.info("[%s] utterance end (%s) %.1fs", self.device_id, reason, len(seg) / DEVICE_IN_RATE)
        self.turn = asyncio.create_task(self._turn(seg))

    # ---------------- 一轮问答 ----------------
    async def _turn(self, samples: np.ndarray):
        r = self.hub.replies
        try:
            text = ""
            if len(samples) >= DEVICE_IN_RATE * 0.3:
                text = await self.hub.speech.asr.transcribe(samples)
            if len(text.strip(" 。，,.!！?？嗯啊呃")) < 1:
                await self._speak_simple(r["not_heard"])
                return
            await self.send_json({"type": "stt", "text": text})
            await self.set_pixel("thinking")
            limit = int(self.cfg["muse"].get("max_background_tasks", 3))
            if self.hub.running_background(self.device_id) >= limit:
                await self._speak_simple(r["too_busy"])
                return
            self.job = self.hub.start_job(self.device_id, text)
            await self._foreground(self.job)
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception("[%s] turn failed", self.device_id)
            try:
                await self._speak_simple(r["error"])
            except Exception:
                pass
        finally:
            if self.state in ("processing", "speaking") and asyncio.current_task() is self.turn:
                self.state = "idle"
            self.job = None
            self.hub.kick(self.device_id)

    async def _speak_simple(self, text: str):
        sp = Speaker(self)
        self.speaker = sp
        await sp.start()
        cleaned = self.hub.cleaner.clean(text)
        for s in self.hub.cleaner.split_sentences(cleaned):
            await sp.say(s, s)
        await sp.finish()
        self.speaker = None

    async def _foreground(self, job):
        """等 Muse 回话；等太久就先回"收到"，转后台，回来了再主动念。"""
        oc = self.cfg["muse"]
        r = self.hub.replies
        ack_after = float(oc.get("ack_after_seconds", 6))
        stall = float(oc.get("stall_seconds", 10))
        asm = SentenceAssembler()
        consumed = 0
        sp: Speaker | None = None
        deadline = time.monotonic() + ack_after
        while True:
            timeout = max(0.05, deadline - time.monotonic()) if sp is None else stall
            try:
                item = await asyncio.wait_for(job.queue.get(), timeout=timeout)
            except asyncio.TimeoutError:
                # 等不及了：先回一句，转后台
                offset = consumed - len(asm.buf) - len(asm._raw)
                await self.hub.to_background(job, offset)
                sp = sp or Speaker(self)
                self.speaker = sp
                if sp.started:
                    ack = r["ack_continue"]
                else:  # 复述原话，听得出它有没有听对
                    tpl = r.get("ack_echo") or r["ack"]
                    ack = tpl.format(heard=echo_head(job.text, int(r.get("echo_max_chars", 25))))
                await sp.say(ack, self.hub.cleaner.clean(ack))
                await sp.finish()
                self.speaker = None
                return
            if item is None:  # Muse 说完了
                tail = asm.flush()
                for s in tail:
                    spoken = self.hub.cleaner.clean(s)
                    if spoken:
                        sp = sp or Speaker(self)
                        self.speaker = sp
                        await sp.say(s, spoken)
                if job.error and (sp is None or not sp.started):
                    msg = r["muse_down"] if "连不上" in job.error else r["error"]
                    log.warning("[%s] muse error: %s", self.device_id, job.error)
                    await self._speak_simple(msg)
                    return
                if sp is None:
                    await self._speak_simple(r["empty"])
                    return
                await sp.finish()
                self.speaker = None
                return
            consumed += len(item)
            for s in asm.feed(item):
                spoken = self.hub.cleaner.clean(s)
                if not spoken:
                    continue
                sp = sp or Speaker(self)
                self.speaker = sp
                await sp.say(s, spoken)
