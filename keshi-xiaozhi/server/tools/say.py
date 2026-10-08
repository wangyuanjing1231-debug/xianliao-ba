"""让小智念一句话（测试主动开口用，不经过 OpenClaw）。

用法（在 server 目录里）：
    .venv\\Scripts\\python tools\\say.py "下午三点开科会"
口令自动从 config.yaml 读取；设备不在线时会排队，等它连上再念。
"""

import json
import os
import sys
import urllib.request

import yaml

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit('用法：python tools/say.py "要念的话"')
    text = " ".join(sys.argv[1:])
    cfg_path = os.path.join(BASE, "config.yaml")
    cfg = yaml.safe_load(open(cfg_path, encoding="utf-8")) if os.path.exists(cfg_path) else {}
    server = (cfg or {}).get("server", {})
    port = int(server.get("port", 8000))
    token = server.get("api_token", "")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/push",
        data=json.dumps({"text": text, "device": "all", "source": "say.py"}).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        print(r.read().decode("utf-8"))


if __name__ == "__main__":
    main()
