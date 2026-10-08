"""小智服务端入口：python main.py [config.yaml]"""

import logging
import logging.handlers
import os
import sys

import yaml
from aiohttp import web

from xzserver.app import build_app

BASE = os.path.dirname(os.path.abspath(__file__))


def load_config(path: str) -> dict:
    with open(os.path.join(BASE, "config.example.yaml"), encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            user = yaml.safe_load(f) or {}
        for k, v in user.items():  # 两层合并：没写的项用默认值
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                for kk, vv in v.items():
                    if isinstance(vv, dict) and isinstance(cfg[k].get(kk), dict):
                        cfg[k][kk].update(vv)
                    else:
                        cfg[k][kk] = vv
            else:
                cfg[k] = v
    return cfg


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BASE, "config.yaml")
    cfg = load_config(path)
    os.makedirs(os.path.join(BASE, "logs"), exist_ok=True)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        handlers=[logging.StreamHandler(sys.stdout),
                  # 电脑从不关机：日志按 5MB 轮换，最多留 5 份，不会把硬盘写满
                  logging.handlers.RotatingFileHandler(os.path.join(BASE, "logs", "server.log"),
                                                       maxBytes=5_000_000, backupCount=5,
                                                       encoding="utf-8")],
    )
    if not os.path.exists(path):
        logging.warning("没找到 %s，先用默认配置跑（OpenClaw token 和推送口令都是空的）", path)
    app = build_app(cfg, BASE)
    web.run_app(app, host=cfg["server"]["host"], port=int(cfg["server"]["port"]), print=None)


if __name__ == "__main__":
    main()
