#!/usr/bin/env python3
"""
quota_service.py - Antigravity Quota Monitor & HTTP Bridge for ESP32 额度屏
- 轮询 Antigravity Gemini 5h 与每周额度
- 轮询 Gemini 网页版（gemini.google.com）5h 与每周额度
- 挂在 本机 0.0.0.0:8888 供板子 (192.168.7.x) 和本机访问
- 5 小时窗口重置自动续期（调用 agy -p "hi"）
- 支持 /api/trigger-alarm 与 /api/clear-alarm 供实机声音测试
- 首页 / 提供可视化三组额度面板，/api/quota 提供 JSON
"""

import json
import logging
import os
import re
import shutil
import socket
import subprocess
import threading
import time
from datetime import datetime, timezone, timedelta
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import urllib.request
import urllib.error

# Patch DNS for daily-cloudcode-pa.googleapis.com to bypass local TUN/fake-ip issues
_orig_getaddrinfo = socket.getaddrinfo
def _patched_getaddrinfo(host, port, *args, **kwargs):
    if host == 'daily-cloudcode-pa.googleapis.com':
        host = '172.217.119.4'
    return _orig_getaddrinfo(host, port, *args, **kwargs)
socket.getaddrinfo = _patched_getaddrinfo

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s'
)
logger = logging.getLogger("quota_service")

PORT = 8888
TOKEN_PATH = Path.home() / '.gemini' / 'antigravity-cli' / 'antigravity-oauth-token'
AGY_BIN = shutil.which("agy") or str(Path.home() / ".local" / "bin" / "agy")
BROWSER_CLI = shutil.which("browser-cli") or "/usr/local/bin/browser-cli"

# Global State
state_lock = threading.Lock()
current_quota = {
    "status": "initializing",
    "updated_at": None,
    "tier": "Google AI Pro",
    
    # 1. Antigravity Gemini 5H
    "gemini_5h_remaining_pct": 100.0,
    "gemini_5h_used_pct": 0.0,
    "gemini_5h_resets_at": None,
    "gemini_5h_reset_seconds": 0,
    "gemini_5h_reset_minutes": 0,
    
    # 2. Antigravity Gemini Weekly (API 未提供与 agy /usage 一致的数据，依规设为 None 显示 --)
    "gemini_weekly_remaining_pct": None,
    "gemini_weekly_used_pct": None,
    "gemini_weekly_resets_at": None,
    "gemini_weekly_reset_seconds": None,
    "gemini_weekly_reset_hours": None,
    
    # 3. Gemini Web (gemini.google.com)
    "gemini_web_status": "initializing",
    "gemini_web_5h_remaining_pct": 100.0,
    "gemini_web_5h_used_pct": 0.0,
    "gemini_web_5h_reset_str": "--:--",
    "gemini_web_weekly_remaining_pct": 100.0,
    "gemini_web_weekly_used_pct": 0.0,
    "gemini_web_weekly_reset_str": "---",
    "gemini_web_updated_at": None,
    
    "server_hour": datetime.now().hour,
    "test_alarm": False
}

last_reset_time_str = None
renew_triggered_for_window = False

def read_token_from_file():
    if not TOKEN_PATH.exists():
        return None
    try:
        data = json.loads(TOKEN_PATH.read_text(encoding='utf-8'))
        tok = data.get('token', {})
        return tok.get('access_token')
    except Exception as e:
        logger.error(f"Failed to read token file: {e}")
        return None

def trigger_agy_refresh():
    """Trigger agy binary to renew its OAuth credentials."""
    logger.info("Triggering agy credential refresh via 'agy models'...")
    try:
        subprocess.run([AGY_BIN, "models"], timeout=30, capture_output=True, check=False)
        time.sleep(1)
    except Exception as e:
        logger.error(f"Failed to run agy models: {e}")

def trigger_agy_keepalive():
    """Send a short prompt to start a new 5h quota window."""
    logger.info("Triggering agy auto-renew ping via 'agy -p hi'...")
    try:
        subprocess.run([AGY_BIN, "-p", "hi"], timeout=30, capture_output=True, check=False)
        logger.info("Auto-renew prompt sent successfully.")
    except Exception as e:
        logger.error(f"Failed to run agy keepalive: {e}")

def parse_iso(ts_str):
    if not ts_str:
        return None
    try:
        clean = ts_str.strip().replace("Z", "+00:00")
        return datetime.fromisoformat(clean)
    except Exception:
        return None

def fetch_antigravity_quota_from_agy():
    """Run agy -p "/usage" --output-format json and extract Gemini Models buckets."""
    try:
        res = subprocess.run(
            [AGY_BIN, "-p", "/usage", "--output-format", "json"],
            capture_output=True, text=True, timeout=25, check=False
        )
        if res.returncode != 0:
            logger.error(f"agy -p /usage failed (code {res.returncode}): {res.stderr}")
            return None
        data = json.loads(res.stdout)
        cmd_data = data.get("command", {}).get("data", {})
        groups = cmd_data.get("groups", [])
        gemini_group = next((g for g in groups if g.get("name") == "Gemini Models"), None)
        if not gemini_group:
            logger.warning("Gemini Models group not found in agy usage output")
            return None
        buckets = {b.get("id"): b for b in gemini_group.get("buckets", [])}
        return {
            "5h": buckets.get("gemini-5h"),
            "weekly": buckets.get("gemini-weekly")
        }
    except Exception as e:
        logger.error(f"Failed to fetch quota via agy: {e}")
        return None

def quota_updater_worker():
    global last_reset_time_str, renew_triggered_for_window
    logger.info("Antigravity Quota updater thread started (using agy -p /usage).")

    while True:
        try:
            agy_data = fetch_antigravity_quota_from_agy()
            if agy_data:
                b_5h = agy_data.get("5h")
                b_weekly = agy_data.get("weekly")
                now_utc = datetime.now(timezone.utc)

                # 1. Gemini 5H
                rem_5h = None
                used_5h = None
                reset_5h_str = None
                sec_left_5h = 0
                min_left_5h = 0
                reset_dt_5h = None

                if b_5h and "remaining_fraction" in b_5h:
                    frac_5h = float(b_5h["remaining_fraction"])
                    rem_5h = round(frac_5h * 100.0, 1)
                    used_5h = round((1.0 - frac_5h) * 100.0, 1)
                    reset_5h_str = b_5h.get("reset_time")
                    reset_dt_5h = parse_iso(reset_5h_str)
                    sec_left_5h = max(0, int((reset_dt_5h - now_utc).total_seconds())) if reset_dt_5h else 0
                    min_left_5h = sec_left_5h // 60

                # 2. Gemini Weekly
                rem_wk = None
                used_wk = None
                reset_wk_str = None
                sec_left_wk = 0
                hrs_left_wk = 0
                reset_dt_wk = None

                if b_weekly and "remaining_fraction" in b_weekly:
                    frac_wk = float(b_weekly["remaining_fraction"])
                    rem_wk = round(frac_wk * 100.0, 1)
                    used_wk = round((1.0 - frac_wk) * 100.0, 1)
                    reset_wk_str = b_weekly.get("reset_time")
                    reset_dt_wk = parse_iso(reset_wk_str)
                    sec_left_wk = max(0, int((reset_dt_wk - now_utc).total_seconds())) if reset_dt_wk else 0
                    hrs_left_wk = sec_left_wk // 3600

                # Check for 5h window transition
                if reset_5h_str and reset_5h_str != last_reset_time_str:
                    logger.info(f"5h window detected: resets at {reset_5h_str} ({min_left_5h} mins left, {rem_5h}% rem)")
                    last_reset_time_str = reset_5h_str
                    renew_triggered_for_window = False

                # Check for auto-renew condition:
                if reset_dt_5h and now_utc >= reset_dt_5h + timedelta(seconds=5) and not renew_triggered_for_window:
                    logger.info(">>> 5h window expired! Auto-renewing window now...")
                    trigger_agy_keepalive()
                    renew_triggered_for_window = True
                    time.sleep(5)
                    continue

                with state_lock:
                    current_quota.update({
                        "status": "ok",
                        "updated_at": datetime.now().astimezone().isoformat(),
                        "tier": "Google AI Pro",
                        "gemini_5h_remaining_pct": rem_5h,
                        "gemini_5h_used_pct": used_5h,
                        "gemini_5h_resets_at": reset_5h_str,
                        "gemini_5h_reset_seconds": sec_left_5h,
                        "gemini_5h_reset_minutes": min_left_5h,
                        "gemini_weekly_remaining_pct": rem_wk,
                        "gemini_weekly_used_pct": used_wk,
                        "gemini_weekly_resets_at": reset_wk_str,
                        "gemini_weekly_reset_seconds": sec_left_wk,
                        "gemini_weekly_reset_hours": hrs_left_wk,
                        "server_hour": datetime.now().hour,
                    })
        except Exception as e:
            logger.error(f"Error in quota updater loop: {e}")

        time.sleep(30)


def fetch_gemini_web_usage_once():
    """Use browser-cli to navigate to https://gemini.google.com/usage and parse numbers."""
    try:
        logger.info("Fetching Gemini Web usage via browser-cli...")
        # 1. Ensure page is loaded
        subprocess.run([BROWSER_CLI, "open", "https://gemini.google.com/usage"],
                       timeout=35, capture_output=True, text=True, check=False)
        
        # 2. Extract DOM values
        js_code = r'''() => {
            const text = document.body.innerText;
            const c = text.match(/当前用量[\s\S]*?已使用\s*(\d+)%/);
            const cr = text.match(/当前用量[\s\S]*?重置时间[：:]\s*([^\n\r]+)/);
            const w = text.match(/每周限额[\s\S]*?已使用\s*(\d+)%/);
            const wr = text.match(/每周限额[\s\S]*?重置时间[：:]\s*([^\n\r]+)/);
            return JSON.stringify({
                used_5h: c ? parseInt(c[1]) : null,
                reset_5h: cr ? cr[1].trim() : null,
                used_weekly: w ? parseInt(w[1]) : null,
                reset_weekly: wr ? wr[1].trim() : null
            });
        }'''
        res = subprocess.run([BROWSER_CLI, "call", "browser_evaluate", json.dumps({"function": js_code})],
                             timeout=35, capture_output=True, text=True, check=False)
        
        parsed = None
        lines = res.stdout.splitlines()
        for i, line in enumerate(lines):
            if '### Result' in line and i + 1 < len(lines):
                val = lines[i+1].strip()
                parsed = json.loads(json.loads(val))
                break
                
        if parsed and parsed.get('used_5h') is not None:
            used_5h = float(parsed['used_5h'])
            rem_5h = round(100.0 - used_5h, 1)
            reset_5h = parsed.get('reset_5h') or "--:--"
            
            used_w = float(parsed.get('used_weekly', 0.0))
            rem_w = round(100.0 - used_w, 1)
            reset_w = parsed.get('reset_weekly') or "---"
            reset_w = re.sub(r'(\d+)月(\d+)日', r'\1-\2 ', reset_w)
            
            with state_lock:
                current_quota.update({
                    "gemini_web_status": "ok",
                    "gemini_web_5h_remaining_pct": rem_5h,
                    "gemini_web_5h_used_pct": used_5h,
                    "gemini_web_5h_reset_str": reset_5h,
                    "gemini_web_weekly_remaining_pct": rem_w,
                    "gemini_web_weekly_used_pct": used_w,
                    "gemini_web_weekly_reset_str": reset_w,
                    "gemini_web_updated_at": datetime.now().astimezone().isoformat(),
                })
            logger.info(f"Gemini Web quota updated: 5h {rem_5h}% rem (resets {reset_5h}), weekly {rem_w}% rem (resets {reset_w})")
            return True
        else:
            logger.warning(f"Could not parse Gemini Web quota: {res.stdout[:200]}")
            return False
    except Exception as e:
        logger.error(f"Error fetching Gemini Web quota: {e}")
        return False

def gemini_web_updater_worker():
    logger.info("Gemini Web updater thread started (30m interval).")
    # Immediate run on startup
    success = fetch_gemini_web_usage_once()
    retry_delay = 60 if not success else 1800
    
    while True:
        time.sleep(retry_delay)
        success = fetch_gemini_web_usage_once()
        retry_delay = 1800 if success else 120

class QuotaHttpHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        if "trigger-alarm" in self.path or "clear-alarm" in self.path or "refresh" in self.path:
            logger.info("%s - %s", self.address_string(), format % args)

    def do_GET(self):
        accept_header = self.headers.get('Accept', '')
        user_agent = self.headers.get('User-Agent', '')

        if self.path in ('/api/quota', '/quota.json') or (self.path == '/' and 'application/json' in accept_header and 'text/html' not in accept_header):
            with state_lock:
                current_quota["server_hour"] = datetime.now().hour
                resp_bytes = json.dumps(current_quota, ensure_ascii=False, indent=2).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(resp_bytes)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(resp_bytes)
            
        elif self.path == '/':
            # Serve friendly visual dashboard
            with state_lock:
                data = dict(current_quota)
                
            q5_rem = data.get("gemini_5h_remaining_pct", 100.0)
            q5_color = "#2ecc71" if q5_rem > 50 else ("#f1c40f" if q5_rem >= 20 else "#e74c3c")
            
            qw_rem = data.get("gemini_weekly_remaining_pct")
            if qw_rem is not None:
                qw_pct_str = f"{qw_rem}%"
                qw_bar_w = f"{qw_rem}%"
                qw_color = "#2ecc71" if qw_rem > 50 else ("#f1c40f" if qw_rem >= 20 else "#e74c3c")
                qw_reset_str = f"{data.get('gemini_weekly_reset_hours', 0) // 24} 天 {data.get('gemini_weekly_reset_hours', 0) % 24} 小时"
                qw_reset_at = str(data.get('gemini_weekly_resets_at', '---'))
                qw_used_str = f"{data.get('gemini_weekly_used_pct', 0.0)}%"
            else:
                qw_pct_str = "--"
                qw_bar_w = "0%"
                qw_color = "#7f8c8d"
                qw_reset_str = "--"
                qw_reset_at = "--"
                qw_used_str = "--"
            
            web5_rem = data.get("gemini_web_5h_remaining_pct", 100.0)
            web5_color = "#2ecc71" if web5_rem > 50 else ("#f1c40f" if web5_rem >= 20 else "#e74c3c")
            
            webw_rem = data.get("gemini_web_weekly_remaining_pct", 100.0)
            webw_color = "#2ecc71" if webw_rem > 50 else ("#f1c40f" if webw_rem >= 20 else "#e74c3c")
            
            html = f"""<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Gemini Quota Dashboard · 额度监控</title>
  <style>
    * {{ box-sizing: border-box; }}
    body {{ background: #0f1117; color: #e1e7ec; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; margin: 0; padding: 24px; text-align: center; }}
    h1 {{ margin-bottom: 6px; font-size: 24px; color: #fff; }}
    .subtitle {{ color: #7f8c8d; font-size: 14px; margin-bottom: 28px; }}
    .grid {{ display: flex; flex-wrap: wrap; gap: 20px; justify-content: center; max-width: 1080px; margin: 0 auto; }}
    .card {{ background: #1a1e29; border: 1px solid #2a3142; border-radius: 12px; padding: 20px; flex: 1; min-width: 280px; max-width: 340px; text-align: left; box-shadow: 0 4px 12px rgba(0,0,0,0.3); }}
    .tag {{ display: inline-block; font-size: 12px; font-weight: bold; padding: 3px 8px; border-radius: 4px; background: #2c3e50; color: #ecf0f1; margin-bottom: 12px; }}
    .card-title {{ font-size: 18px; font-weight: bold; margin-bottom: 8px; color: #fff; }}
    .pct-big {{ font-size: 46px; font-weight: 800; margin: 12px 0 4px 0; }}
    .bar-bg {{ background: #242b3d; height: 16px; border-radius: 8px; overflow: hidden; margin: 12px 0; }}
    .bar-fill {{ height: 100%; border-radius: 8px; transition: width 0.3s; }}
    .info-row {{ font-size: 13px; color: #95a5a6; margin: 6px 0; }}
    .info-val {{ color: #ecf0f1; font-weight: 600; }}
    .actions {{ margin-top: 32px; display: flex; gap: 12px; justify-content: center; flex-wrap: wrap; }}
    .btn {{ display: inline-block; padding: 10px 18px; border-radius: 6px; font-weight: bold; font-size: 13px; text-decoration: none; cursor: pointer; border: none; }}
    .btn-red {{ background: #e74c3c; color: #fff; }}
    .btn-green {{ background: #27ae60; color: #fff; }}
    .btn-blue {{ background: #2980b9; color: #fff; }}
  </style>
</head>
<body>
  <h1>Gemini 配额监控面板</h1>
  <div class="subtitle">Antigravity 双周期配额 & Gemini 网页版配额 · ESP32 额度屏数据中枢</div>
  
  <div class="grid">
    <!-- Card 1: Antigravity 5H -->
    <div class="card">
      <span class="tag">PAGE 1 · 默认主页</span>
      <div class="card-title">Antigravity 5H 额度</div>
      <div class="pct-big" style="color: {q5_color};">{q5_rem}%</div>
      <div class="bar-bg"><div class="bar-fill" style="width: {q5_rem}%; background: {q5_color};"></div></div>
      <div class="info-row">距重置: <span class="info-val">{data.get('gemini_5h_reset_minutes', 0) // 60} 小时 {data.get('gemini_5h_reset_minutes', 0) % 60} 分钟</span></div>
      <div class="info-row">重置时刻: <span class="info-val">{data.get('gemini_5h_resets_at', '---')}</span></div>
      <div class="info-row">订阅档位: <span class="info-val">{data.get('tier', 'Google AI Pro')}</span></div>
    </div>

    <!-- Card 2: Antigravity Weekly -->
    <div class="card">
      <span class="tag">PAGE 2 · 滚动页</span>
      <div class="card-title">Antigravity 每周额度</div>
      <div class="pct-big" style="color: {qw_color};">{qw_pct_str}</div>
      <div class="bar-bg"><div class="bar-fill" style="width: {qw_bar_w}; background: {qw_color};"></div></div>
      <div class="info-row">距重置: <span class="info-val">{qw_reset_str}</span></div>
      <div class="info-row">重置时刻: <span class="info-val">{qw_reset_at}</span></div>
      <div class="info-row">已使用比例: <span class="info-val">{qw_used_str}</span></div>
    </div>

    <!-- Card 3: Gemini Web -->
    <div class="card">
      <span class="tag">PAGE 3 · 网页版独立池</span>
      <div class="card-title">Gemini 网页版额度</div>
      <div class="pct-big" style="color: {web5_color};">{web5_rem}%</div>
      <div class="bar-bg"><div class="bar-fill" style="width: {web5_rem}%; background: {web5_color};"></div></div>
      <div class="info-row">5H 重置时间: <span class="info-val">{data.get('gemini_web_5h_reset_str', '--:--')}</span> (已用 {data.get('gemini_web_5h_used_pct', 0)}%)</div>
      <div class="info-row">每周额度: <span class="info-val" style="color: {webw_color};">{webw_rem}% 剩余</span> (已用 {data.get('gemini_web_weekly_used_pct', 0)}%)</div>
      <div class="info-row">每周重置: <span class="info-val">{data.get('gemini_web_weekly_reset_str', '---')}</span></div>
      <div class="info-row">抓取来源: <span class="info-val">Playwright MCP (每 30 分钟)</span></div>
    </div>
  </div>

  <div class="actions">
    <a href="/api/trigger-alarm" class="btn btn-red">触发实机测试报警 (嘀嘀循环)</a>
    <a href="/api/clear-alarm" class="btn btn-green">解除测试报警</a>
    <a href="/api/refresh-web" class="btn btn-blue">立即刷新网页版额度</a>
    <a href="/api/quota" class="btn" style="background:#4a5568;color:#fff;">查看完整 JSON 接口</a>
  </div>
</body>
</html>
"""
            resp_bytes = html.encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(resp_bytes)))
            self.end_headers()
            self.wfile.write(resp_bytes)
            
        elif self.path == '/api/trigger-alarm':
            with state_lock:
                current_quota["test_alarm"] = True
            resp = json.dumps({"status": "ok", "test_alarm": True, "message": "Alarm triggered! Board will beep and listen for clap."}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(resp)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(resp)
            
        elif self.path == '/api/clear-alarm':
            with state_lock:
                current_quota["test_alarm"] = False
            resp = json.dumps({"status": "ok", "test_alarm": False, "message": "Alarm cleared."}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(resp)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(resp)
            
        elif self.path == '/api/refresh-web':
            threading.Thread(target=fetch_gemini_web_usage_once, daemon=True).start()
            resp = json.dumps({"status": "ok", "message": "Background web quota refresh triggered."}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(resp)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(resp)
            
        else:
            self.send_error(404, "Not Found")

    def do_POST(self):
        if self.path in ('/api/trigger-alarm', '/api/clear-alarm', '/api/refresh-web'):
            self.do_GET()
        else:
            self.send_error(404, "Not Found")

def run_server():
    server = HTTPServer(('0.0.0.0', PORT), QuotaHttpHandler)
    logger.info(f"Quota HTTP service running on http://0.0.0.0:{PORT}/")
    server.serve_forever()

if __name__ == '__main__':
    t1 = threading.Thread(target=quota_updater_worker, daemon=True)
    t1.start()
    t2 = threading.Thread(target=gemini_web_updater_worker, daemon=True)
    t2.start()
    run_server()
