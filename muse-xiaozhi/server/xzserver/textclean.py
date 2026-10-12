"""把大模型的文字整理成适合朗读的中文。

做三件事：
1. 去掉 Markdown 格式、代码、链接、表情符号（不念符号）。
2. 把英文缩写、单位改成中文读法（本地朗读模型只会读中文）。
3. 按句子切开，好让第一句先念、后面边合成边念。
"""

from __future__ import annotations

import re

# 英文字母的中文近似读法（拼缩写用，比如 CT -> 西提）
LETTER_CN = {
    "A": "诶", "B": "比", "C": "西", "D": "迪", "E": "伊", "F": "艾弗", "G": "吉",
    "H": "艾尺", "I": "爱", "J": "杰", "K": "开", "L": "艾勒", "M": "艾姆", "N": "恩",
    "O": "欧", "P": "皮", "Q": "扣", "R": "阿", "S": "艾斯", "T": "提", "U": "优",
    "V": "维", "W": "达不溜", "X": "艾克斯", "Y": "歪", "Z": "贼德",
}

# 默认读法表；config.yaml 里的 lexicon 会覆盖/追加（不区分大小写）
DEFAULT_LEXICON = {
    "Muse": "缪斯",
    "PACS": "派克斯",
    "WiFi": "无线网",
    "Wi-Fi": "无线网",
    "OK": "好的",
    "App": "应用",
    "PPT": "皮皮提",
    "PDF": "皮迪艾弗",
    "Excel": "表格",
    "Word": "文档",
}

UNIT_CN = [
    (r"(?<=\d)\s*mm\b", "毫米"),
    (r"(?<=\d)\s*cm\b", "厘米"),
    (r"(?<=\d)\s*km\b", "公里"),
    (r"(?<=\d)\s*m[lL]\b", "毫升"),
    (r"(?<=\d)\s*mg\b", "毫克"),
    (r"(?<=\d)\s*kg\b", "千克"),
    (r"(?<=\d)\s*min\b", "分钟"),
    (r"(?<=\d)\s*HU\b", "亨氏单位"),
]

_EMOJI_RE = re.compile(
    "["
    "\U0001F000-\U0001FAFF"  # 各类表情、符号
    "\U00002600-\U000027BF"  # 杂项符号、丁巴特
    "\U00002B00-\U00002BFF"
    "\U0000FE00-\U0000FE0F"  # 变体选择符
    "\U0000200D"             # 零宽连接符
    "\U000020E3"
    "]+"
)

_HARD_STOP = "。！？!?；;…\n"
_SOFT_STOP = "，,：:、"


class TextCleaner:
    def __init__(self, lexicon: dict | None = None, max_acronym_len: int = 6):
        merged = dict(DEFAULT_LEXICON)
        if lexicon:
            merged.update({str(k): str(v) for k, v in lexicon.items()})
        # 长的词先替换，避免 "Wi-Fi" 被拆
        self._lex = sorted(merged.items(), key=lambda kv: -len(kv[0]))
        self._lex_lower = {k.lower(): v for k, v in merged.items()}
        self._max_acr = max_acronym_len

    # ---------- 1. 去格式 ----------
    @staticmethod
    def strip_markdown(text: str) -> str:
        t = text.replace("\r\n", "\n")
        t = re.sub(r"<think>.*?</think>", "", t, flags=re.S)
        t = re.sub(r"```.*?```", "。代码就不念了。", t, flags=re.S)
        t = re.sub(r"```.*$", "", t, flags=re.S)  # 没闭合的代码块
        t = re.sub(r"`([^`]*)`", r"\1", t)
        t = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", t)  # 图片
        t = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", t)  # 链接只留文字
        t = re.sub(r"https?://\S+", "", t)
        t = re.sub(r"^\s{0,3}#{1,6}\s*", "", t, flags=re.M)  # 标题
        t = re.sub(r"^\s*>+\s?", "", t, flags=re.M)  # 引用
        t = re.sub(r"^\s*([-*+]|\d+[.)、])\s+", "", t, flags=re.M)  # 列表符号
        t = re.sub(r"^\s*[-*_]{3,}\s*$", "", t, flags=re.M)  # 分隔线
        t = re.sub(r"^\s*\|?\s*:?-{2,}.*$", "", t, flags=re.M)  # 表格分隔行
        t = t.replace("|", "，")
        t = re.sub(r"(\*\*|__|~~)(.+?)\1", r"\2", t)
        t = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"\1", t)
        t = t.replace("*", "").replace("#", "")
        t = _EMOJI_RE.sub("", t)
        return t

    # ---------- 2. 读法 ----------
    def _word(self, m: re.Match) -> str:
        w = m.group(0)
        hit = self._lex_lower.get(w.lower())
        if hit is not None:
            return hit
        core = w.replace("-", "").replace(".", "")
        if core.isupper() and core.isalnum() and len(core) <= self._max_acr:
            return "".join(LETTER_CN.get(ch, ch) for ch in core)
        return w  # 普通英文单词原样留着（朗读模型会跳过）

    def to_spoken(self, text: str) -> str:
        t = text
        for k, v in self._lex:
            if not re.fullmatch(r"[A-Za-z][A-Za-z0-9.\-]*", k):  # 中文词条直接替换
                t = t.replace(k, v)
        t = re.sub(r"(\d+(?:\.\d+)?)\s*%", r"百分之\1", t)
        t = re.sub(r"(\d)\s*[~～—–-]\s*(\d)", r"\1到\2", t)
        t = t.replace("℃", "摄氏度").replace("→", "到").replace("±", "正负")
        t = t.replace("×", "乘").replace("&", "和").replace("≥", "大于等于").replace("≤", "小于等于")
        t = t.replace(">", "大于").replace("<", "小于").replace("=", "等于")
        for pat, rep in UNIT_CN:
            t = re.sub(pat, rep, t)
        t = re.sub(r"[A-Za-z][A-Za-z0-9.\-]*", self._word, t)
        t = re.sub(r"[ \t]+", " ", t)
        t = re.sub(r"\n{2,}", "\n", t)
        t = re.sub(r"([，,。！？!?；;])[，,。]+", r"\1", t)
        return t.strip(" \n，,")

    def clean(self, text: str) -> str:
        return self.to_spoken(self.strip_markdown(text or ""))

    # ---------- 3. 切句 ----------
    @staticmethod
    def split_sentences(text: str, soft_limit: int = 40, min_len: int = 6) -> list[str]:
        out, buf = [], ""
        for ch in text:
            buf += ch
            if ch in _HARD_STOP or (ch in _SOFT_STOP and len(buf) >= soft_limit):
                s = buf.strip().lstrip(_HARD_STOP + _SOFT_STOP)
                if s:
                    out.append(s)
                buf = ""
        if buf.strip():
            out.append(buf.strip())
        # 太短的句子并到前一句，避免一卡一卡
        merged: list[str] = []
        for s in out:
            core = s.strip(_HARD_STOP + _SOFT_STOP + " ")
            if not core:
                continue
            if merged and len(core) < min_len:
                merged[-1] += s
            else:
                merged.append(s)
        return merged

    def shorten(self, text: str, max_chars: int, tail: str) -> str:
        """长任务的结果太长时只念前面一段，并提示后面还有。"""
        if len(text) <= max_chars:
            return text
        cut = text[:max_chars]
        for stop in "。！？!?；;":
            pos = cut.rfind(stop)
            if pos >= max_chars // 2:
                cut = cut[: pos + 1]
                break
        return cut + tail


class SentenceAssembler:
    """把大模型一点点吐出来的字拼成完整句子（代码块整段跳过）。"""

    def __init__(self, soft_limit: int = 40, min_len: int = 6):
        self.buf = ""
        self.soft_limit = soft_limit
        self.min_len = min_len
        self._raw = ""        # 还没判断完是不是 ``` 的尾巴
        self._in_code = False

    def _filter_code(self, delta: str) -> str:
        self._raw += delta
        out = ""
        while True:
            pos = self._raw.find("```")
            if pos < 0:
                # 末尾可能是半个 ```，先留着
                keep = len(self._raw) - len(self._raw.rstrip("`"))
                safe = self._raw[: len(self._raw) - keep] if keep else self._raw
                self._raw = self._raw[len(safe):]
                if not self._in_code:
                    out += safe
                return out
            if not self._in_code:
                out += self._raw[:pos] + "。代码就不念了。"
            self._in_code = not self._in_code
            self._raw = self._raw[pos + 3:]

    def feed(self, delta: str) -> list[str]:
        self.buf += self._filter_code(delta)
        out = []
        while True:
            idx = -1
            for i, ch in enumerate(self.buf):
                if ch in _HARD_STOP and len(self.buf[: i + 1].strip(_HARD_STOP + " ")) >= self.min_len:
                    idx = i
                    break
                if ch in _SOFT_STOP and i + 1 >= self.soft_limit:
                    idx = i
                    break
            if idx < 0:
                break
            # 连在一起的句末标点一起带走
            while idx + 1 < len(self.buf) and self.buf[idx + 1] in _HARD_STOP:
                idx += 1
            s, self.buf = self.buf[: idx + 1], self.buf[idx + 1 :]
            s = s.strip().lstrip(_HARD_STOP + _SOFT_STOP)
            if s.strip(_HARD_STOP + _SOFT_STOP + " "):
                out.append(s)
        return out

    def flush(self) -> list[str]:
        if not self._in_code:
            self.buf += self._raw.replace("`", "")
        self._raw = ""
        s, self.buf = self.buf, ""
        s = s.strip().lstrip(_HARD_STOP + _SOFT_STOP)
        return [s] if s.strip(_HARD_STOP + _SOFT_STOP + " ") else []


def echo_head(text: str, max_chars: int = 25, cut_chars: int = 20) -> str:
    """复述用：短句全念；长句念到第一个停顿处，最多 cut_chars 个字。"""
    t = re.sub(r"\s+", " ", text or "").strip().rstrip(_HARD_STOP + _SOFT_STOP + "，。 ")
    if len(t) <= max_chars:
        return t
    head = t[:cut_chars]
    m = re.search(r"[" + re.escape(_HARD_STOP + _SOFT_STOP) + r"]", head)
    if m and m.start() >= 4:
        head = head[:m.start()]
    return head.rstrip(_HARD_STOP + _SOFT_STOP + " ")
