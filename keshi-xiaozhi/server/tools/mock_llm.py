"""测试用的假大模型（OpenAI 兼容接口）。不花钱、不联网，用来在没有 API key 时把整条链路跑通。

规则（看最后一句用户的话）：
- 含"慢慢"：等 SLOW 秒再回答（模拟长任务）
- 含"卡一下"：先说一句，停 15 秒，再说一句（模拟干到一半在调工具）
- 含"让小智说"：调用 xiaozhi_say 工具（模拟从手机让小智念一句）
- 其他：马上回答一句
"""

import asyncio
import json
import sys
import time

from aiohttp import web

SLOW = float(sys.argv[2]) if len(sys.argv) > 2 else 12
LOG = []


def _text(content):
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "".join(p.get("text", "") for p in content if isinstance(p, dict))
    return ""


def _chunk(cid, delta=None, finish=None, tool_calls=None):
    d = {} if delta is None else {"content": delta}
    if tool_calls is not None:
        d = {"tool_calls": tool_calls}
    return "data: " + json.dumps({
        "id": cid, "object": "chat.completion.chunk", "created": int(time.time()), "model": "mock-1",
        "choices": [{"index": 0, "delta": d, "finish_reason": finish}],
    }, ensure_ascii=False) + "\n\n"


async def chat(request: web.Request):
    body = await request.json()
    msgs = body.get("messages", [])
    tools = [t.get("function", {}).get("name") for t in body.get("tools") or []]
    users = [_text(m.get("content")) for m in msgs if m.get("role") == "user"]
    # OpenClaw 会在用户原话后面再附一条内部上下文消息，所以看最后两条
    last_user = "\n".join(users[-2:])
    system = "\n".join(_text(m.get("content")) for m in msgs if m.get("role") in ("system", "developer"))
    LOG.append({"t": time.time(), "last_user": last_user[-200:], "tools": len(tools), "tool_names": tools,
                "has_xiaozhi_say": "xiaozhi_say" in tools, "voice_prompt": "语音小设备" in system,
                "last_role": msgs[-1].get("role") if msgs else None})
    cid = f"mock-{int(time.time() * 1000)}"
    resp = web.StreamResponse(headers={"Content-Type": "text/event-stream"})
    await resp.prepare(request)

    async def say(text, step=0.03):
        for i in range(0, len(text), 4):
            await resp.write(_chunk(cid, text[i:i + 4]).encode())
            await asyncio.sleep(step)

    last_role = msgs[-1].get("role") if msgs else ""
    req_idx = max((i for i, m in enumerate(msgs) if m.get("role") == "user" and "让小智说" in _text(m.get("content"))), default=-1)
    tool_done = req_idx >= 0 and any(m.get("role") == "tool" for m in msgs[req_idx:])
    if last_role == "tool" or tool_done:
        await say("已经让小智念出来了。")
    elif "让小智说" in last_user and "xiaozhi_say" in tools:
        call = [{"index": 0, "id": "call_1", "type": "function",
                 "function": {"name": "xiaozhi_say", "arguments": json.dumps({"text": "下午三点在示教室开会，请大家准时参加。"}, ensure_ascii=False)}}]
        await resp.write(_chunk(cid, tool_calls=call).encode())
        await resp.write(_chunk(cid, finish="tool_calls").encode())
        await resp.write(b"data: [DONE]\n\n")
        return resp
    elif "慢慢" in last_user:
        await asyncio.sleep(SLOW)
        await say("资料整理好了，一共三份，已经放在桌面的科室资料文件夹里。")
    elif "卡一下" in last_user:
        await say("好的，我先查一下。")
        await asyncio.sleep(15)
        await say("查到了，下周三下午有空位。")
    else:
        await say("今天天气不错，适合出去走走。下午记得带伞。")
    await resp.write(_chunk(cid, finish="stop").encode())
    await resp.write(b"data: [DONE]\n\n")
    return resp


async def models(request):
    return web.json_response({"object": "list", "data": [{"id": "mock-1", "object": "model"}]})


async def log(request):
    return web.json_response(LOG)


app = web.Application()
app.router.add_post("/v1/chat/completions", chat)
app.router.add_get("/v1/models", models)
app.router.add_get("/log", log)

if __name__ == "__main__":
    web.run_app(app, host="127.0.0.1", port=int(sys.argv[1]) if len(sys.argv) > 1 else 9999, print=None)
