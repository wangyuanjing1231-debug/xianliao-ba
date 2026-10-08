"""总调度：管所有在线设备、所有交给 OpenClaw 的活、所有待播的通知。"""

from __future__ import annotations

import asyncio
import logging
import time
import uuid
from dataclasses import dataclass, field

from .audio import DownlinkEncoder, FRAME_MS, ogg_opus
from .openclaw_client import OpenClawClient, OpenClawError
from .textclean import TextCleaner

log = logging.getLogger("hub")


def compact_id(device_id: str) -> str:
    return "".join(ch for ch in device_id.lower() if ch.isalnum()) or "unknown"


@dataclass
class Job:
    """交给 OpenClaw 的一件事。前台（设备正等着听）或后台（已经回了"收到"）。"""

    id: str
    device_id: str
    session_key: str
    text: str
    queue: asyncio.Queue = field(default_factory=asyncio.Queue)
    full_text: str = ""
    error: str | None = None
    finished: bool = False
    background: bool = False
    bg_offset: int = 0
    started: float = field(default_factory=time.monotonic)
    task: asyncio.Task | None = None


@dataclass
class Pending:
    text: str
    kind: str
    created: float = field(default_factory=time.monotonic)
    attempts: int = 0


class Hub:
    def __init__(self, cfg: dict, speech, http):
        self.cfg = cfg
        self.speech = speech
        self.cleaner = TextCleaner(cfg.get("lexicon") or {})
        self.client = OpenClawClient(cfg["openclaw"], http)
        self.replies = cfg["replies"]
        self.sessions: dict[str, object] = {}       # device_id -> DeviceSession
        self.jobs: dict[str, Job] = {}
        self.busy_keys: set[str] = set()
        self.pending: dict[str, list[Pending]] = {}  # device_id -> 待播通知
        self.working: dict[str, int] = {}           # device_id -> 后台活数
        self.notify_files: dict[str, dict] = {}
        self.known: set[str] = set()                # 连过的设备

    # ---------------- 设备上下线 ----------------
    def register(self, sess):
        old = self.sessions.get(sess.device_id)
        self.sessions[sess.device_id] = sess
        self.known.add(sess.device_id)
        if old is not None and old is not sess:
            asyncio.create_task(old.close("replaced"))

    def unregister(self, sess):
        if self.sessions.get(sess.device_id) is sess:
            del self.sessions[sess.device_id]

    async def on_ready(self, sess):
        """设备握手完成：补发状态和没播出去的通知。"""
        if self.working.get(sess.device_id, 0) > 0:
            await sess.set_pixel("pet_working")
        self.kick(sess.device_id)

    # ---------------- 交给 OpenClaw ----------------
    def _pick_key(self, device_id: str) -> str:
        base = f"{self.cfg['openclaw'].get('session_prefix', 'xiaozhi')}-{compact_id(device_id)}"
        if base not in self.busy_keys:
            return base
        n = 2
        while f"{base}-{n}" in self.busy_keys:
            n += 1
        return f"{base}-{n}"

    def running_background(self, device_id: str) -> int:
        return sum(1 for j in self.jobs.values() if j.device_id == device_id and j.background and not j.finished)

    def start_job(self, device_id: str, text: str) -> Job:
        key = self._pick_key(device_id)
        self.busy_keys.add(key)
        job = Job(id=uuid.uuid4().hex[:8], device_id=device_id, session_key=key, text=text)
        self.jobs[job.id] = job
        job.task = asyncio.create_task(self._run_job(job), name=f"job-{job.id}")
        return job

    async def _run_job(self, job: Job):
        max_s = float(self.cfg["openclaw"].get("max_task_minutes", 30)) * 60
        try:
            async def pump():
                async for delta in self.client.stream(job.session_key, job.text):
                    job.full_text += delta
                    await job.queue.put(delta)
            await asyncio.wait_for(pump(), timeout=max_s)
        except asyncio.TimeoutError:
            job.error = "超时"
        except asyncio.CancelledError:
            job.error = "已取消"
            raise
        except OpenClawError as e:
            job.error = str(e)
            log.warning("job %s failed: %s", job.id, e)
        except Exception as e:  # 网络等意外
            job.error = f"{type(e).__name__}: {e}"
            log.exception("job %s crashed", job.id)
        finally:
            job.finished = True
            self.busy_keys.discard(job.session_key)
            await job.queue.put(None)
            if job.background:
                await self._finish_background(job)
            self.jobs.pop(job.id, None)
            log.info("job %s done in %.1fs bg=%s err=%s", job.id, time.monotonic() - job.started,
                     job.background, job.error)

    def cancel_job(self, job: Job):
        if job and not job.finished and not job.background and job.task:
            job.task.cancel()

    async def to_background(self, job: Job, offset: int):
        """前台等不及了：转后台，屏幕切"干活中"。"""
        if job.background:
            return
        job.background = True
        job.bg_offset = max(0, offset)
        self.working[job.device_id] = self.working.get(job.device_id, 0) + 1
        sess = self.sessions.get(job.device_id)
        if sess:
            await sess.set_pixel("pet_working")
        if job.finished:  # 极少数：转后台时刚好办完
            await self._finish_background(job)

    async def _finish_background(self, job: Job):
        if getattr(job, "_delivered", False):
            return
        job._delivered = True
        n = max(0, self.working.get(job.device_id, 1) - 1)
        self.working[job.device_id] = n
        r = self.replies
        if job.error and job.error != "已取消":
            reason = job.error if len(job.error) <= 30 else job.error[:30]
            text = f"{r['task_failed']}原因是：{reason}"
        elif job.error == "已取消":
            return
        else:
            rest = self.cleaner.clean(job.full_text[job.bg_offset:])
            if not rest:
                rest = "已经办完了。"
            limit = int(self.cfg["openclaw"].get("max_spoken_chars", 200))
            text = r["done_prefix"] + self.cleaner.shorten(rest, limit, r["too_long_tail"])
        self.push(job.device_id, text, kind="task_done")
        sess = self.sessions.get(job.device_id)
        if sess and n == 0:
            await sess.set_pixel("pet_idle")

    # ---------------- 主动开口（通知） ----------------
    def push(self, device_id: str, text: str, kind: str = "push") -> dict:
        if device_id in ("", "all", "*"):
            # 在线的设备马上念；暂时不在线但连过的设备，等它连回来再念
            targets = sorted(set(self.sessions.keys()) | self.known)
            if not targets:
                return {"ok": False, "error": "还没有设备连过这台服务器"}
        else:
            targets = [device_id]
        for d in targets:
            queue = self.pending.setdefault(d, [])
            if any(p.text == text for p in queue):
                continue  # 同一句话还在排队，不重复念（防止大模型连续调用多次）
            queue.append(Pending(text=text, kind=kind))
            self.kick(d)
        return {"ok": True, "devices": targets}

    def kick(self, device_id: str):
        sess = self.sessions.get(device_id)
        if sess is not None and sess.ready and self.pending.get(device_id):
            asyncio.create_task(self._drain(sess))

    async def _drain(self, sess):
        if sess.draining:
            return
        sess.draining = True
        try:
            ttl = float(self.cfg["server"].get("pending_minutes", 30)) * 60
            while self.pending.get(sess.device_id):
                if not sess.ready or sess.closed:
                    return
                if sess.state != "idle":
                    await asyncio.sleep(1.0)
                    continue
                item = self.pending[sess.device_id][0]
                if time.monotonic() - item.created > ttl:
                    self.pending[sess.device_id].pop(0)
                    log.info("drop expired notify for %s: %s", sess.device_id, item.text[:30])
                    continue
                await asyncio.sleep(1.0)  # 刚说完话，留一口气
                if sess.state != "idle":
                    continue
                ok = await self._notify(sess, item)
                if ok or item.attempts >= 3:
                    self.pending[sess.device_id].pop(0)
                    if ok and item.kind == "task_done":
                        await sess.set_pixel("pet_done")
                else:
                    await asyncio.sleep(5.0)
        finally:
            sess.draining = False

    async def _notify(self, sess, item: Pending) -> bool:
        """生成整段语音文件，发 notify 让设备自己来拉；设备来拉了就算送达。"""
        item.attempts += 1
        spoken = self.cleaner.clean(item.text)
        sentences = self.cleaner.split_sentences(spoken) or ["收到一条消息。"]
        enc = DownlinkEncoder()
        packets, subtitles, t = [], [], 0.0
        for s in sentences:
            x = await self.speech.tts.synth(s)
            pk = enc.encode(x)
            subtitles.append({"start_ms": int(t * 1000), "text": s})
            packets += pk
            t += len(pk) * FRAME_MS / 1000
        if not packets:
            return True
        nid = uuid.uuid4().hex
        fetched = asyncio.Event()
        self.notify_files[nid] = {"data": ogg_opus(packets, pre_skip_rate_samples=enc.lookahead),
                                   "expires": time.monotonic() + 600, "fetched": fetched}
        url = f"http://{sess.host}/notify/{nid}.ogg"
        sess.state = "notifying"
        try:
            await sess.send_json({"type": "notify", "audio_url": url, "subtitles": subtitles})
            log.info("notify -> %s (%.1fs) %s", sess.device_id, t, spoken[:40])
            try:
                await asyncio.wait_for(fetched.wait(), timeout=8)
            except asyncio.TimeoutError:
                log.warning("device %s did not fetch notify audio (busy?), will retry", sess.device_id)
                return False
            await asyncio.sleep(t + 1.5)  # 提示音 + 播放时间
            return True
        finally:
            if sess.state == "notifying":
                sess.state = "idle"

    def get_notify_file(self, nid: str) -> bytes | None:
        now = time.monotonic()
        for k in [k for k, v in self.notify_files.items() if v["expires"] < now]:
            self.notify_files.pop(k, None)
        item = self.notify_files.get(nid)
        if not item:
            return None
        item["fetched"].set()
        return item["data"]

    # ---------------- 心跳 ----------------
    async def keepalive_loop(self):
        interval = float(self.cfg["server"].get("keepalive_seconds", 25))
        while True:
            await asyncio.sleep(interval)
            for sess in list(self.sessions.values()):
                if sess.ready and sess.supports_keepalive and sess.state == "idle":
                    try:
                        await sess.send_json({"type": "keepalive"})
                    except Exception:
                        pass

    def status(self) -> dict:
        return {
            "devices": [
                {"device_id": s.device_id, "state": s.state, "keepalive": s.supports_keepalive,
                 "working": self.working.get(s.device_id, 0),
                 "pending": len(self.pending.get(s.device_id, []))}
                for s in self.sessions.values()
            ],
            "jobs": [{"id": j.id, "device": j.device_id, "background": j.background,
                      "seconds": round(time.monotonic() - j.started, 1)} for j in self.jobs.values()],
        }
