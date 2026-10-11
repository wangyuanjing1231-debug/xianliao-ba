本机语音模型放这里（约 300MB，不进仓库）。运行下面这条会自动下载并核对：

    .venv\Scripts\python tools\download_models.py

下载完应该有：
- silero_vad.onnx（判断有没有在说话）
- sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17\（语音转文字）
- matcha-icefall-zh-baker\ 和 vocos-22khz-univ.onnx（文字转语音，标贝女声）
