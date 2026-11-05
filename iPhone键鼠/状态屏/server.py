import json, os, threading, time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
from zoneinfo import ZoneInfo

HOME = os.path.expanduser("~/派单账本")
DATA = os.path.join(HOME, "账本.json")
PORT = 8893
BINDS = ["127.0.0.1", "100.81.27.34"]
STUCK = 30 * 60
FADE = 30 * 60
DROP = 6 * 3600
TZ = ZoneInfo("Asia/Shanghai")
lock = threading.Lock()

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

def clean(s):
    return (s or "").replace("\t", " ").replace("\n", " ").strip()

def pick(q, *names):
    for n in names:
        if q.get(n):
            return q[n][0]
    return ""

def view(db, now):
    rows = []
    for o in db["orders"]:
        st, faded = o["state"], 0
        if st == "完成":
            age = now - o["updated"]
            if age > DROP:
                continue
            faded = 1 if age > FADE else 0
            key = (4 if faded else 3, age)
        elif st == "等你":
            age = now - o["updated"]
            key = (0, -age)
        else:
            age = now - o["sent"]
            if now - o["updated"] > STUCK:
                st = "卡住"
                key = (1, -age)
            else:
                key = (2, -age)
        rows.append((key, dict(id=o["id"], state=st, worker=o["worker"], task=o["task"],
                               why=o.get("why", "") if st == "等你" else "", age=int(age), faded=faded)))
    rows.sort(key=lambda r: r[0])
    return [r[1] for r in rows]

class H(BaseHTTPRequestHandler):
    def reply(self, code, text):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlsplit(self.path)
        path, q = u.path.rstrip("/") or "/", parse_qs(u.query)
        now = time.time()
        with lock:
            db = load()
            if path == "/new":
                w, t = clean(pick(q, "工人", "w")), clean(pick(q, "事", "t"))
                if not w or not t:
                    return self.reply(400, "要 w= 和 t=\n")
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
                for o in db["orders"]:
                    if str(o["id"]) == oid:
                        o["state"], o["updated"] = st, now
                        o["why"] = clean(pick(q, "因", "y"))[:4] if st == "等你" else ""
                        save(db)
                        return self.reply(200, "ok\n")
                return self.reply(404, f"没有 {oid} 号单\n")
            if path == "/screen":
                with open(os.path.join(HOME, "板子最后来取.txt"), "w") as f:
                    f.write(f"{datetime.now(TZ):%m-%d %H:%M:%S} {self.client_address[0]}\n")
                rows = view(db, now)
                waiting = sum(1 for r in rows if r["state"] == "等你")
                today = datetime.now(TZ).date()
                today_orders = [o for o in db["orders"] if datetime.fromtimestamp(o.get("sent", 0), TZ).date() == today]
                today_done = sum(1 for o in today_orders if o.get("state") == "完成")
                today_total = len(today_orders)
                out = [f"{today_done}/{today_total}\t{waiting}\t0"]
                code = {"等你": "w", "卡住": "s", "在跑": "r", "完成": "d"}
                out.append("E\t" + ",".join(f"{r['id']}:{code[r['state']]}" for r in rows))
                for r in rows[:4]:
                    out.append("\t".join(str(x) for x in (r["state"], r["worker"], r["task"], r["why"], r["age"], r["faded"])))
                return self.reply(200, "\n".join(out) + "\n")
            if path == "/all":
                rows = view(db, now)
                return self.reply(200, "".join(f"{r['id']}\t{r['state']}\t{r['worker']}\t{r['task']}\t{r['why']}\t{r['age'] // 60}分\n" for r in rows) or "没有单\n")
        self.reply(404, "路径：/new /r /screen /all\n")

    def log_message(self, fmt, *args):
        pass

if __name__ == "__main__":
    os.makedirs(HOME, exist_ok=True)
    servers = [ThreadingHTTPServer((b, PORT), H) for b in BINDS]
    for s in servers[1:]:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    servers[0].serve_forever()
