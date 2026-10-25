#!/usr/bin/env python3
import http.server
import socketserver
import socket
import threading
import json
import os
import time

STATE_FILE = "/root/desk-pet-state.json"
HTTP_PORT = 8088
UDP_PORT = 8888

def get_state():
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE, "r") as f:
                data = json.load(f)
                if data.get("state") == "done" and (time.time() - data.get("time", 0)) > 30:
                    data["state"] = "idle"
                    data["summary"] = "Ready"
                return data
        except Exception:
            pass
    return {
        "state": "idle",
        "progress": "",
        "step": 0,
        "tool": "none",
        "summary": "Ready",
        "time": int(time.time()),
        "ts": time.strftime("%Y-%m-%d %H:%M:%S")
    }

class PetHTTPHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        state_data = get_state()
        body = json.dumps(state_data, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass

def udp_broadcast_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        sock.bind(("0.0.0.0", UDP_PORT))
    except Exception as e:
        return

    while True:
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode("utf-8", errors="ignore").strip()
            if "DISCOVER" in msg or "XPERIA" in msg or "PET" in msg:
                reply = json.dumps({
                    "service": "desk-pet",
                    "port": HTTP_PORT,
                    "time": int(time.time())
                }).encode("utf-8")
                sock.sendto(reply, addr)
        except Exception:
            pass

def main():
    t = threading.Thread(target=udp_broadcast_listener, daemon=True)
    t.start()

    class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True

    with ThreadedTCPServer(("0.0.0.0", HTTP_PORT), PetHTTPHandler) as httpd:
        httpd.serve_forever()

if __name__ == "__main__":
    main()
