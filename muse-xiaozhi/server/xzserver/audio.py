"""音频小工具：Opus 编解码、重采样、生成 Ogg Opus 文件（给设备的 notify 用）。"""

from __future__ import annotations

import os
import random
import struct

import numpy as np


def _prepare_opus_dll() -> None:
    """Windows 没有自带 opus 库：有 server/bin/opus.dll 就用它；Linux 用系统的 libopus0。"""
    if os.name != "nt":
        return
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bindir = os.environ.get("XZ_OPUS_DIR") or os.path.join(here, "bin")
    if os.path.isfile(os.path.join(bindir, "opus.dll")):
        # opuslib_next 用 ctypes.util.find_library("opus") 找库，Windows 上它只搜 PATH
        os.environ["PATH"] = bindir + os.pathsep + os.environ.get("PATH", "")
        try:
            os.add_dll_directory(bindir)
        except (AttributeError, OSError):
            pass


_prepare_opus_dll()
import opuslib_next  # noqa: E402

DEVICE_IN_RATE = 16000     # 设备上行：16kHz 单声道 Opus
DEVICE_OUT_RATE = 24000    # 下行给设备：24kHz（这块板子的喇叭就是 24k，不用再重采样）
FRAME_MS = 60


def pcm16_to_float(pcm: bytes) -> np.ndarray:
    return np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0


def float_to_pcm16(x: np.ndarray) -> bytes:
    return (np.clip(x, -1.0, 1.0) * 32767.0).astype(np.int16).tobytes()


def resample(x: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    """频域重采样（一句话一句话做，长度只有几秒，速度够用、音质干净）。"""
    if sr_in == sr_out or len(x) == 0:
        return x.astype(np.float32)
    pad = int(sr_in * 0.05)  # 两头垫 50ms 静音，避免首尾咔哒声
    xp = np.concatenate([np.zeros(pad, np.float32), x.astype(np.float32), np.zeros(pad, np.float32)])
    n_out = int(round(len(xp) * sr_out / sr_in))
    spec = np.fft.rfft(xp)
    bins = n_out // 2 + 1
    out_spec = np.zeros(bins, dtype=np.complex128)
    m = min(len(spec), bins)
    out_spec[:m] = spec[:m]
    y = np.fft.irfft(out_spec, n_out) * (n_out / len(xp))
    cut = int(round(pad * sr_out / sr_in))
    return y[cut: len(y) - cut].astype(np.float32)


class UplinkDecoder:
    """设备上行的 Opus -> 16k PCM。"""

    def __init__(self, rate: int = DEVICE_IN_RATE):
        self.rate = rate
        self.dec = opuslib_next.Decoder(rate, 1)
        self.max_frame = rate * 120 // 1000

    def decode(self, packet: bytes) -> np.ndarray:
        pcm = self.dec.decode(packet, self.max_frame)
        return pcm16_to_float(pcm)


class DownlinkEncoder:
    """24k PCM -> 60ms 一帧的 Opus 包。"""

    def __init__(self, rate: int = DEVICE_OUT_RATE, frame_ms: int = FRAME_MS):
        self.rate = rate
        self.frame = rate * frame_ms // 1000
        self.enc = opuslib_next.Encoder(rate, 1, opuslib_next.APPLICATION_AUDIO)

    @property
    def lookahead(self) -> int:
        try:
            return int(self.enc.lookahead)
        except Exception:
            return self.rate * 65 // 10000

    def encode(self, x: np.ndarray) -> list[bytes]:
        n = len(x)
        if n == 0:
            return []
        rem = n % self.frame
        if rem:
            x = np.concatenate([x, np.zeros(self.frame - rem, np.float32)])
        pcm = float_to_pcm16(x)
        step = self.frame * 2
        return [self.enc.encode(pcm[i: i + step], self.frame) for i in range(0, len(pcm), step)]


# ---------------- Ogg Opus 封装（RFC 7845） ----------------

def _crc_table() -> list[int]:
    table = []
    for i in range(256):
        r = i << 24
        for _ in range(8):
            r = ((r << 1) ^ 0x04C11DB7) if (r & 0x80000000) else (r << 1)
        table.append(r & 0xFFFFFFFF)
    return table


_CRC = _crc_table()


def _ogg_crc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = ((crc << 8) ^ _CRC[((crc >> 24) & 0xFF) ^ b]) & 0xFFFFFFFF
    return crc


def _page(serial: int, seq: int, granule: int, packets: list[bytes], flags: int) -> bytes:
    lacing = bytearray()
    body = bytearray()
    for p in packets:
        n = len(p)
        while n >= 255:
            lacing.append(255)
            n -= 255
        lacing.append(n)
        body += p
    header = struct.pack("<4sBBqIIIB", b"OggS", 0, flags, granule, serial, seq, 0, len(lacing))
    page = bytearray(header + lacing + body)
    struct.pack_into("<I", page, 22, _ogg_crc(bytes(page)))
    return bytes(page)


def ogg_opus(packets: list[bytes], rate: int = DEVICE_OUT_RATE, frame_ms: int = FRAME_MS,
             pre_skip_rate_samples: int = 0) -> bytes:
    """把 Opus 包封装成单声道 Ogg Opus 文件。"""
    serial = random.randint(1, 0x7FFFFFFF)
    pre_skip = pre_skip_rate_samples * 48000 // rate
    head = b"OpusHead" + struct.pack("<BBHIhB", 1, 1, pre_skip, rate, 0, 0)
    vendor = b"keshi-xiaozhi"
    tags = b"OpusTags" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0)
    out = [_page(serial, 0, 0, [head], 0x02), _page(serial, 1, 0, [tags], 0)]
    seq = 2
    granule = pre_skip
    per_packet = 48 * frame_ms
    chunk: list[bytes] = []
    segs = 0
    for i, p in enumerate(packets):
        need = len(p) // 255 + 1
        if chunk and (segs + need > 255 or len(chunk) >= 50):
            out.append(_page(serial, seq, granule, chunk, 0))
            seq += 1
            chunk, segs = [], 0
        chunk.append(p)
        segs += need
        granule += per_packet
    out.append(_page(serial, seq, granule, chunk, 0x04))  # 最后一页带 EOS
    return b"".join(out)


def parse_ogg_packets(data: bytes) -> tuple[dict, list[bytes]]:
    """测试用：把 Ogg 拆回 OpusHead 信息和音频包（同时校验 CRC）。"""
    pos, packets, partial, info = 0, [], b"", {}
    while pos < len(data):
        if data[pos:pos + 4] != b"OggS":
            raise ValueError("bad capture pattern")
        nseg = data[pos + 26]
        lacing = data[pos + 27: pos + 27 + nseg]
        size = sum(lacing)
        page = bytearray(data[pos: pos + 27 + nseg + size])
        crc = struct.unpack_from("<I", page, 22)[0]
        struct.pack_into("<I", page, 22, 0)
        if _ogg_crc(bytes(page)) != crc:
            raise ValueError("bad crc")
        body = data[pos + 27 + nseg: pos + 27 + nseg + size]
        off = 0
        for l in lacing:
            partial += body[off: off + l]
            off += l
            if l < 255:
                packets.append(partial)
                partial = b""
        pos += 27 + nseg + size
    head = packets.pop(0)
    info = {"channels": head[9], "rate": struct.unpack_from("<I", head, 12)[0]}
    packets.pop(0)  # OpusTags
    return info, packets
