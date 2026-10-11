"""用一个假的 musegadget 测去程、回程：假命令收到消息后，按消息里写的路径把回复写进收件箱。"""

import asyncio
import os
import stat
import sys

import pytest

from xzserver.muse_client import MuseClient, MuseError

FAKE = """#!{py}
import re, sys
args = sys.argv[1:]
open({log!r}, "w", encoding="utf-8").write("\\n".join(args))
if {reply!r} is not None:
    path = re.search(r"(/\\S+\\.txt)", args[-1]).group(1)
    open(path, "w", encoding="utf-8").write({reply!r})
print("Sent to your Muse.")
"""


def make_fake(tmp_path, reply):
    log = tmp_path / "args.txt"
    cmd = tmp_path / "musegadget"
    cmd.write_text(FAKE.format(py=sys.executable, log=str(log), reply=reply), encoding="utf-8")
    cmd.chmod(cmd.stat().st_mode | stat.S_IEXEC)
    return str(cmd), log


async def collect(client, text):
    return [x async for x in client.stream("k", text)]


@pytest.mark.skipif(os.name == "nt", reason="假命令用的是 shebang 脚本")
def test_round_trip(tmp_path):
    cmd, log = make_fake(tmp_path, "前列腺 PI-RADS 4 分，建议穿刺。")
    client = MuseClient({"command": cmd, "session_id": "11111111-2222-3333-4444-555555555555",
                         "inbox_dir": str(tmp_path / "inbox"), "reply_timeout_seconds": 5,
                         "prompt_template": "#{id} {text} -> {path}"})
    out = asyncio.run(collect(client, "PI-RADS 四分怎么办"))
    assert out == ["前列腺 PI-RADS 4 分，建议穿刺。"]
    args = log.read_text(encoding="utf-8").split("\n")
    assert args[:3] == ["send-user-msg", "--session-id", "11111111-2222-3333-4444-555555555555"]
    assert "PI-RADS 四分怎么办" in args[3]
    assert os.listdir(tmp_path / "inbox") == []       # 念过的回复删掉


@pytest.mark.skipif(os.name == "nt", reason="假命令用的是 shebang 脚本")
def test_no_reply_times_out(tmp_path):
    cmd, _ = make_fake(tmp_path, None)
    client = MuseClient({"command": cmd, "inbox_dir": str(tmp_path / "inbox"),
                         "reply_timeout_seconds": 1})
    with pytest.raises(MuseError, match="没有回话"):
        asyncio.run(collect(client, "你好"))


def test_missing_command(tmp_path):
    client = MuseClient({"command": str(tmp_path / "nope"), "inbox_dir": str(tmp_path / "inbox")})
    with pytest.raises(MuseError, match="连不上"):
        asyncio.run(collect(client, "你好"))
