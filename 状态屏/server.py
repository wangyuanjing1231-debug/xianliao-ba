import json, os, subprocess, sys, threading, time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
from zoneinfo import ZoneInfo

HOME = os.path.expanduser("~/派单账本")
DATA = os.path.join(HOME, "账本.json")
CFG_PATH = os.path.join(HOME, "config.json")
TTS_DIR = os.path.join(HOME, "tts")
EDGE_TTS = os.path.expanduser("~/.local/bin/edge-tts")
TTS_VOICE = "zh-CN-XiaoxiaoNeural"
PORT = 8893
BINDS = ["127.0.0.1", "100.81.27.34"]
STUCK = 60 * 60
FADE = 30 * 60
DROP = 2 * 3600
EXPIRE = 6 * 3600
DEFAULT_CFG = {
    "wait_beep": 3,
    "stuck_beep": 2,
    "tts_repeat": 2,
    "quiet_min": 20,
    "mic_thresh": 200,
}
TZ = ZoneInfo("Asia/Shanghai")
lock = threading.Lock()
tts_lock = threading.Lock()


def load():
    try:
        with open(DATA, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {"next": 1, "orders": []}


def save(db):
    tmp = DATA + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(db, f, ensure_ascii=False, indent=1)
    os.replace(tmp, DATA)


def load_cfg():
    cfg = dict(DEFAULT_CFG)
    try:
        with open(CFG_PATH, encoding="utf-8") as f:
            user = json.load(f)
        for k in DEFAULT_CFG:
            if k in user:
                cfg[k] = int(user[k])
    except Exception:
        pass
    return cfg


def ensure_cfg():
    if not os.path.exists(CFG_PATH):
        with open(CFG_PATH, "w", encoding="utf-8") as f:
            json.dump(DEFAULT_CFG, f, ensure_ascii=False, indent=2)
            f.write("\n")


def clean(s):
    return (s or "").replace("\t", " ").replace("\n", " ").strip()


def pick(q, *names):
    for n in names:
        if q.get(n):
            return q[n][0]
    return ""


def gen_tts(oid, text):
    text = clean(text)
    if not text:
        return False
    os.makedirs(TTS_DIR, exist_ok=True)
    out_path = os.path.join(TTS_DIR, f"{oid}.pcm")
    mp3_tmp = f"/tmp/paidan_tts_{oid}_{os.getpid()}.mp3"
    pcm_tmp = os.path.join(TTS_DIR, f"{oid}.pcm.tmp.{os.getpid()}")
    speak_text = (" " + text) if text.startswith("-") else text
    with tts_lock:
        for attempt in range(2):
            try:
                subprocess.run(
                    [EDGE_TTS, "--voice", TTS_VOICE, "--text", speak_text, "--write-media", mp3_tmp],
                    check=True, timeout=12, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
                )
                subprocess.run(
                    ["ffmpeg", "-y", "-i", mp3_tmp, "-ac", "1", "-ar", "16000", "-t", "15",
                     "-f", "s16le", "-acodec", "pcm_s16le", pcm_tmp],
                    check=True, timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
                )
                if os.path.getsize(pcm_tmp) > 0:
                    os.replace(pcm_tmp, out_path)
                    return True
            except Exception as e:
                print(f"[tts] attempt {attempt + 1} failed for {oid}: {e}", file=sys.stderr, flush=True)
            finally:
                for p in (mp3_tmp, pcm_tmp):
                    try:
                        os.remove(p)
                    except OSError:
                        pass
    return False


def remove_tts(oid):
    try:
        os.remove(os.path.join(TTS_DIR, f"{oid}.pcm"))
    except OSError:
        pass


def view(db, now, hide=True):
    rows = []
    for o in db["orders"]:
        st, faded = o["state"], 0
        if st == "完成":
            age = now - o["updated"]
            if age > DROP:
                continue
            faded = 1 if age > FADE else 0
            key = (4 if faded else 3, age)
        elif hide and now - o["updated"] > EXPIRE:
            continue
        elif st == "等你":
            age = now - o["updated"]
            key = (0, -age)
        else:
            age = now - o["sent"]
            if now - o["updated"] > STUCK:
                st = "卡住"
                key = (5, age)
            else:
                key = (2, age)
        rows.append((key, dict(id=o["id"], state=st, worker=o["worker"], task=o["task"],
                               why=o.get("why", "") if st == "等你" else "", age=int(age), faded=faded)))
    rows.sort(key=lambda r: r[0])
    return [r[1] for r in rows]


class H(BaseHTTPRequestHandler):
    def reply_bytes(self, code, body, ctype="text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def reply(self, code, text):
        self.reply_bytes(code, text.encode("utf-8"), "text/plain; charset=utf-8")

    def do_GET(self):
        u = urlsplit(self.path)
        path, q = u.path.rstrip("/") or "/", parse_qs(u.query)
        now = time.time()
        if path == "/new":
            w, t = clean(pick(q, "工人", "w")), clean(pick(q, "事", "t"))
            if not w or not t:
                return self.reply(400, "要 w= 和 t=\n")
            with lock:
                db = load()
                oid = db["next"]
                db["next"] += 1
                db["orders"].append(dict(id=oid, worker=w, task=t, sent=now, updated=now, state="在跑", why=""))
                save(db)
            return self.reply(200, f"{oid}\n")

        if path in ("/报", "/r"):
            oid, st = pick(q, "单", "id"), clean(pick(q, "态", "s"))
            st = {"wait": "等你", "done": "完成", "run": "在跑", "接着": "在跑"}.get(st, st)
            if st not in ("等你", "完成", "在跑"):
                return self.reply(400, "s= 只能是 wait、done、run\n")
            task_text = None
            with lock:
                db = load()
                for o in db["orders"]:
                    if str(o["id"]) == oid:
                        o["state"], o["updated"] = st, now
                        o["why"] = clean(pick(q, "因", "y"))[:4] if st == "等你" else ""
                        task_text = o.get("task", "")
                        save(db)
                        break
            if task_text is None:
                return self.reply(404, f"没有 {oid} 号单\n")
            if st == "等你":
                gen_tts(oid, task_text)
            else:
                remove_tts(oid)
            return self.reply(200, "ok\n")

        if path == "/screen":
            with lock:
                db = load()
                with open(os.path.join(HOME, "板子最后来取.txt"), "w") as f:
                    f.write(f"{datetime.now(TZ):%m-%d %H:%M:%S} {self.client_address[0]}\n")
                rows = view(db, now)
                waiting = sum(1 for r in rows if r["state"] == "等你")
                today = datetime.now(TZ).date()
                today_orders = [o for o in db["orders"] if datetime.fromtimestamp(o.get("sent", 0), TZ).date() == today]
                today_done = sum(1 for o in today_orders if o.get("state") == "完成")
                today_total = len(today_orders)
            cfg = load_cfg()
            cfg_str = f"{cfg['wait_beep']},{cfg['stuck_beep']},{cfg['tts_repeat']},{cfg['quiet_min']},{cfg['mic_thresh']}"
            out = [f"{today_done}/{today_total}\t{waiting}\t{cfg_str}"]
            code = {"等你": "w", "卡住": "r", "在跑": "r", "完成": "d"}
            out.append("E\t" + ",".join(f"{r['id']}:{code[r['state']]}" for r in rows))
            display_rows = rows[:4]
            for r in display_rows:
                st = "在跑" if r["state"] == "卡住" else r["state"]
                out.append("\t".join(str(x) for x in (st, r["worker"], r["task"], r["why"], r["age"], r["faded"])))
            return self.reply(200, "\n".join(out) + "\n")

        if path == "/tts":
            oid = clean(pick(q, "id", "单"))
            if not oid.isdigit():
                return self.reply(400, "要 id=\n")
            pcm_path = os.path.join(TTS_DIR, f"{oid}.pcm")
            if not (os.path.isfile(pcm_path) and os.path.getsize(pcm_path) > 0):
                task_text = None
                with lock:
                    db = load()
                    for o in db["orders"]:
                        if str(o["id"]) == oid:
                            task_text = o.get("task", "")
                            break
                if task_text:
                    gen_tts(oid, task_text)
            if os.path.isfile(pcm_path) and os.path.getsize(pcm_path) > 0:
                with open(pcm_path, "rb") as f:
                    data = f.read()
                return self.reply_bytes(200, data, "application/octet-stream")
            return self.reply(404, "no tts\n")

        if path == "/all":
            with lock:
                db = load()
                rows = view(db, now, hide=False)
            return self.reply(200, "".join(f"{r['id']}\t{r['state']}\t{r['worker']}\t{r['task']}\t{r['why']}\t{r['age'] // 60}分\n" for r in rows) or "没有单\n")

        self.reply(404, "路径：/new /r /screen /tts /all\n")

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    os.makedirs(HOME, exist_ok=True)
    os.makedirs(TTS_DIR, exist_ok=True)
    ensure_cfg()
    servers = [ThreadingHTTPServer((b, PORT), H) for b in BINDS]
    for s in servers[1:]:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    servers[0].serve_forever()
