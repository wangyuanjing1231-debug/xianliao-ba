"""本机语音：断句（VAD）、识别（ASR）、朗读（TTS），全部用 sherpa-onnx 在 CPU 上跑。"""

from __future__ import annotations

import asyncio
import logging
import os
import threading
import time

import numpy as np
import sherpa_onnx

from .audio import DEVICE_IN_RATE, DEVICE_OUT_RATE, DownlinkEncoder, resample

log = logging.getLogger("speech")


def _p(base: str, path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(base, path)


class Vad:
    """每个连接一份。负责判断"人开始说了""人说完了"。"""

    def __init__(self, cfg: dict, base_dir: str):
        c = sherpa_onnx.VadModelConfig()
        c.silero_vad.model = _p(base_dir, cfg["model"])
        c.silero_vad.threshold = float(cfg.get("threshold", 0.5))
        c.silero_vad.min_silence_duration = float(cfg.get("min_silence_seconds", 0.8))
        c.silero_vad.min_speech_duration = float(cfg.get("min_speech_seconds", 0.25))
        c.silero_vad.max_speech_duration = float(cfg.get("max_utterance_seconds", 30))
        c.silero_vad.window_size = 512
        c.sample_rate = DEVICE_IN_RATE
        c.num_threads = 1
        self._cfg = c
        self.vad = sherpa_onnx.VoiceActivityDetector(c, buffer_size_in_seconds=60)
        self.speech_seen = False

    def reset(self):
        self.vad.reset()
        self.speech_seen = False

    def feed(self, samples: np.ndarray) -> np.ndarray | None:
        """喂一段音频；如果一句话刚说完，返回这句话的音频。"""
        self.vad.accept_waveform(samples)
        if self.vad.is_speech_detected():
            self.speech_seen = True
        if not self.vad.empty():
            seg = np.array(self.vad.front.samples, dtype=np.float32)
            self.vad.pop()
            return seg
        return None

    def flush(self) -> np.ndarray | None:
        self.vad.flush()
        if not self.vad.empty():
            seg = np.array(self.vad.front.samples, dtype=np.float32)
            self.vad.pop()
            return seg
        return None


class Asr:
    def __init__(self, cfg: dict, base_dir: str, threads: int):
        d = _p(base_dir, cfg["model_dir"])
        model = os.path.join(d, "model.int8.onnx")
        if not os.path.exists(model):
            model = os.path.join(d, "model.onnx")
        self.rec = sherpa_onnx.OfflineRecognizer.from_sense_voice(
            model=model,
            tokens=os.path.join(d, "tokens.txt"),
            num_threads=threads,
            use_itn=True,
            language=cfg.get("language", "zh"),
        )
        self._lock = threading.Lock()

    def _run(self, samples: np.ndarray) -> str:
        with self._lock:
            s = self.rec.create_stream()
            s.accept_waveform(DEVICE_IN_RATE, samples)
            self.rec.decode_stream(s)
            return s.result.text.strip()

    async def transcribe(self, samples: np.ndarray) -> str:
        t0 = time.monotonic()
        text = await asyncio.to_thread(self._run, samples)
        log.info("ASR %.2fs audio -> %.2fs: %s", len(samples) / DEVICE_IN_RATE, time.monotonic() - t0, text)
        return text


class Tts:
    """把一句中文合成成发给设备的 Opus 包。默认 Matcha（标贝女声）+ vocos 声码器。"""

    def __init__(self, cfg: dict, base_dir: str, threads: int):
        kind = cfg.get("type", "matcha")
        d = _p(base_dir, cfg["model_dir"])
        mc = sherpa_onnx.OfflineTtsModelConfig(num_threads=threads)
        fsts = []
        if kind == "matcha":
            mc.matcha = sherpa_onnx.OfflineTtsMatchaModelConfig(
                acoustic_model=os.path.join(d, cfg.get("acoustic_model", "model-steps-3.onnx")),
                vocoder=_p(base_dir, cfg["vocoder"]),
                lexicon=os.path.join(d, "lexicon.txt"),
                tokens=os.path.join(d, "tokens.txt"),
                dict_dir=os.path.join(d, "dict"),
            )
            fsts = ["phone.fst", "date.fst", "number.fst"]
        elif kind == "vits":
            mc.vits = sherpa_onnx.OfflineTtsVitsModelConfig(
                model=os.path.join(d, cfg.get("model", "model.onnx")),
                lexicon=os.path.join(d, "lexicon.txt"),
                tokens=os.path.join(d, "tokens.txt"),
                dict_dir=os.path.join(d, "dict"),
            )
            fsts = ["date.fst", "phone.fst", "number.fst", "new_heteronym.fst"]
        else:
            raise ValueError(f"unknown tts type {kind}")
        rule = ",".join(os.path.join(d, f) for f in fsts if os.path.exists(os.path.join(d, f)))
        self.tts = sherpa_onnx.OfflineTts(sherpa_onnx.OfflineTtsConfig(model=mc, rule_fsts=rule))
        self.sid = int(cfg.get("speaker_id", 0))
        self.speed = float(cfg.get("speed", 1.0))
        self.volume = float(cfg.get("volume", 1.0))
        self._lock = threading.Lock()

    def _run(self, text: str) -> np.ndarray:
        with self._lock:
            a = self.tts.generate(text, sid=self.sid, speed=self.speed)
        x = np.asarray(a.samples, dtype=np.float32)
        if len(x) == 0:
            return x
        x = resample(x, a.sample_rate, DEVICE_OUT_RATE)
        peak = float(np.max(np.abs(x))) or 1.0
        return x * min(self.volume, 0.95 / peak)  # 统一音量，防爆音

    async def synth(self, text: str) -> np.ndarray:
        """返回 24kHz 的音频（numpy float32）。"""
        t0 = time.monotonic()
        x = await asyncio.to_thread(self._run, text)
        log.debug("TTS %.2fs -> %.2fs audio: %s", time.monotonic() - t0, len(x) / DEVICE_OUT_RATE, text)
        return x


class Speech:
    """服务启动时加载一次，所有设备共用。"""

    def __init__(self, cfg: dict, base_dir: str):
        threads = int(cfg.get("threads", 2))
        self.cfg = cfg
        self.base_dir = base_dir
        t0 = time.monotonic()
        self.asr = Asr(cfg["asr"], base_dir, threads)
        self.tts = Tts(cfg["tts"], base_dir, threads)
        Vad(cfg["vad"], base_dir)  # 先试加载一次，模型路径错了启动时就报
        log.info("speech models loaded in %.1fs", time.monotonic() - t0)

    def new_vad(self) -> Vad:
        return Vad(self.cfg["vad"], self.base_dir)
