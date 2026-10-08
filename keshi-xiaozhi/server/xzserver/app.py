"""HTTP + WebSocket 入口（一个端口全包：设备连接、OTA 地址下发、推送接口、通知音频）。"""

from __future__ import annotations

import asyncio
import hmac
import logging
import time

import aiohttp
from aiohttp import web

from .hub import Hub
from .session import DeviceSession
from .speech import Speech

log = logging.getLogger("app")


def _bearer(request: web.Request) -> str:
    auth = request.headers.get("Authorization", "")
    if auth.lower().startswith("bearer "):
        return auth[7:].strip()
    return request.query.get("token", "")


def build_app(cfg: dict, base_dir: str) -> web.Application:
    app = web.Application(client_max_size=1024 * 1024)
    srv = cfg["server"]

    async def on_startup(app):
        app["http"] = aiohttp.ClientSession()
        speech = Speech(cfg["speech"], base_dir)
        app["hub"] = Hub(cfg, speech, app["http"])
        app["keepalive"] = asyncio.get_running_loop().create_task(app["hub"].keepalive_loop())

    async def on_cleanup(app):
        if app.get("keepalive"):
            app["keepalive"].cancel()
        for s in list(app["hub"].sessions.values()):
            await s.close("server shutdown")
        await app["http"].close()

    # ---------- 设备 WebSocket ----------
    async def ws_handler(request: web.Request):
        allowed = [d.lower() for d in (srv.get("allowed_devices") or [])]
        dev = (request.headers.get("Device-Id") or request.query.get("device-id") or "").lower()
        if allowed and dev not in allowed:
            log.warning("reject device %s (not in allowed_devices)", dev)
            raise web.HTTPForbidden(text="device not allowed")
        token = (srv.get("device_token") or "").strip()
        if token and not hmac.compare_digest(_bearer(request), token):
            raise web.HTTPUnauthorized(text="bad token")
        ws = web.WebSocketResponse(heartbeat=None, max_msg_size=4 * 1024 * 1024)
        await ws.prepare(request)
        await DeviceSession(request.app["hub"], ws, request).run()
        return ws

    # ---------- OTA：告诉设备连哪里 ----------
    async def ota_handler(request: web.Request):
        host = srv.get("public_host") or request.host
        if ":" not in host:
            host = f"{host}:{srv.get('port', 8000)}"
        body = {
            "server_time": {"timestamp": int(time.time() * 1000), "timezone_offset": int(srv.get("timezone_offset_minutes", 480))},
            "websocket": {"url": f"ws://{host}/xiaozhi/v1/", "token": srv.get("device_token") or "", "version": 1},
        }
        return web.json_response(body)

    # ---------- 推送：让小智开口（给 OpenClaw 插件用） ----------
    async def push_handler(request: web.Request):
        api_token = (srv.get("api_token") or "").strip()
        if not api_token or not hmac.compare_digest(_bearer(request), api_token):
            raise web.HTTPUnauthorized(text="bad token")
        try:
            data = await request.json()
        except Exception:
            raise web.HTTPBadRequest(text="json body required")
        text = str(data.get("text") or "").strip()
        if not text:
            raise web.HTTPBadRequest(text="text required")
        if len(text) > 600:
            text = text[:600]
        device = str(data.get("device") or "all").lower()
        res = request.app["hub"].push(device, text, kind=str(data.get("kind") or "push"))
        log.info("push(%s) via api: %s -> %s", data.get("source", "?"), text[:40], res)
        return web.json_response(res)

    async def status_handler(request: web.Request):
        api_token = (srv.get("api_token") or "").strip()
        if api_token and not hmac.compare_digest(_bearer(request), api_token):
            raise web.HTTPUnauthorized(text="bad token")
        return web.json_response(request.app["hub"].status())

    async def notify_file(request: web.Request):
        data = request.app["hub"].get_notify_file(request.match_info["nid"])
        if data is None:
            raise web.HTTPNotFound()
        return web.Response(body=data, content_type="audio/ogg")

    async def root(request: web.Request):
        return web.Response(text="xiaozhi server ok\n")

    app.router.add_get("/", root)
    for p in ("/xiaozhi/v1/", "/xiaozhi/v1"):
        app.router.add_get(p, ws_handler)
    for p in ("/xiaozhi/ota/", "/xiaozhi/ota"):
        app.router.add_get(p, ota_handler)
        app.router.add_post(p, ota_handler)
    app.router.add_post("/api/push", push_handler)
    app.router.add_get("/api/status", status_handler)
    app.router.add_get("/notify/{nid}.ogg", notify_file)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app
