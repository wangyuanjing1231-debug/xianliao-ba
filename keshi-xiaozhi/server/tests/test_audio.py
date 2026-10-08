import numpy as np

from xzserver.audio import (DEVICE_OUT_RATE, FRAME_MS, DownlinkEncoder, UplinkDecoder,
                            ogg_opus, parse_ogg_packets, resample)


def test_resample_length():
    x = np.zeros(22050, np.float32)
    y = resample(x, 22050, 24000)
    assert abs(len(y) - 24000) <= 2


def test_opus_ogg_roundtrip():
    t = np.arange(DEVICE_OUT_RATE) / DEVICE_OUT_RATE
    x = (0.3 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    enc = DownlinkEncoder()
    packets = enc.encode(x)
    assert len(packets) >= 1000 // FRAME_MS
    data = ogg_opus(packets, pre_skip_rate_samples=enc.lookahead)
    head, got = parse_ogg_packets(data)  # 里面会核对每一页的校验码
    assert got == packets
    assert head["channels"] == 1


def test_uplink_decoder_16k():
    import opuslib_next
    e = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_VOIP)
    pcm = (np.zeros(960, np.int16)).tobytes()
    pkt = e.encode(pcm, 960)
    y = UplinkDecoder().decode(pkt)
    assert len(y) == 960
