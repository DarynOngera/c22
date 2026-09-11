#!/usr/bin/env python3
"""Local telemetry receiver for the Android service demo.

POSTs to /telemetry are validated and appended to build/received.jsonl.
Summary is written to stdout so the demo can be tailed live.
"""
import http.server
import json
import os
import sys

HOST = os.environ.get("TELEMETRY_HOST", "0.0.0.0")
PORT = int(os.environ.get("TELEMETRY_PORT", "8080"))
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "received.jsonl")

os.makedirs(os.path.dirname(LOG), exist_ok=True)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        try:
            events = json.loads(raw)
            n = len(events) if isinstance(events, list) else 1
            with open(LOG, "a") as fh:
                if isinstance(events, list):
                    for e in events:
                        fh.write(json.dumps(e) + "\n")
                else:
                    fh.write(json.dumps(events) + "\n")
            sample = raw[:400].decode(errors="replace")
            print(f"[{PORT}] received {n} event(s): {sample}")
        except Exception as e:  # noqa: BLE001
            print(f"[{PORT}] bad payload ({e}): {raw[:200]!r}")
        sys.stdout.flush()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print(f"telemetry receiver listening on http://{HOST}:{PORT}/telemetry -> {LOG}")
    http.server.ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()