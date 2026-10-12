from xzserver.textclean import SentenceAssembler, TextCleaner


def test_markdown_and_symbols_are_not_read():
    c = TextCleaner()
    out = c.clean("## 标题\n- **结论**：正常\n| a | b |\n看这里 https://example.com/x 好的 😀")
    for bad in ("#", "*", "|", "http", "😀", "- "):
        assert bad not in out
    assert "结论" in out and "正常" in out


def test_numbers_units_and_lexicon():
    c = TextCleaner({"PACS": "派克斯"})
    out = c.clean("PACS 里 2~3 个病例，概率约 5%，体温 38.5℃，结节 3mm，CT 已出")
    assert "派克斯" in out
    assert "2到3" in out
    assert "百分之5" in out
    assert "摄氏度" in out
    assert "毫米" in out
    assert "CT" not in out  # 英文缩写按字母念


def test_code_block_is_skipped():
    out = TextCleaner().clean("代码如下：\n```python\nprint(1)\n```\n就这样")
    assert "print" not in out and "代码就不念了" in out


def test_shorten_adds_tail():
    c = TextCleaner()
    text = "一二三四五六七八九十。" * 30
    out = c.shorten(text, 50, "后面还有。")
    assert len(out) <= 50 + len("后面还有。")
    assert out.endswith("后面还有。")


def test_streaming_sentences():
    a = SentenceAssembler()
    got = []
    for d in ["你好呀朋友", "，今天", "是周四。明天", "是周五", "！再见了朋友们", "```py\nx=1\n```", "收尾一句话。"]:
        got += a.feed(d)
    got += a.flush()
    assert got[0] == "你好呀朋友，今天是周四。"
    assert "代码就不念了。" in got
    assert got[-1] == "收尾一句话。"
    assert not any("x=1" in s for s in got)


def test_echo_head_short_and_long():
    from xzserver.textclean import echo_head
    assert echo_head("帮我查一下明天谁值班。") == "帮我查一下明天谁值班"
    long = "帮我把下周三的门诊，改到周四下午，然后通知一下护士站和住院总"
    assert echo_head(long) == "帮我把下周三的门诊"
    no_stop = "帮我把下周三下午的门诊全部改到周四下午然后通知护士站"
    assert echo_head(no_stop) == no_stop[:20]
