#!/usr/bin/env python3
"""Test-only HTTP fixture; runs core_tests without an external server.
Usage: python3 tests/http_fixture.py /absolute/path/to/core_tests
This helper is not part of any installed application or binary package.
"""
# Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
import base64
import http.server
import json
import mimetypes
import pathlib
import subprocess
import sys
import threading
import time
import urllib.parse


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, status, body=b"", headers=None):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def listing(self, data):
        account = "anonymous" if getattr(self.server, "gui", False) else "fixture-user"
        authorization = self.headers.get("Authorization", "")
        if authorization.startswith("Basic "):
            try:
                # Expose only the synthetic test account name, never its password.
                account = base64.b64decode(authorization[6:], validate=True).decode("utf-8").partition(":")[0] or account
            except (ValueError, UnicodeError):
                pass
        data.setdefault("acct", account)
        data.setdefault("perms", ["read", "get"])
        data.setdefault("srvinf", "Local fixture</span> // <span>Read-only test data")
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query, keep_blank_values=True)
        if query.get("ls") != ["v"]:
            return self.send(200, json.dumps(data).encode(), {"Content-Type": "application/json"})
        lines = ["# acct: " + data["acct"], "# perms: " + repr(data["perms"]),
                 "# srvinf: " + data["srvinf"].replace("</span> // <span>", " // ")]
        for directory in (True, False):
            for entry in data["dirs" if directory else "files"]:
                stamp = time.strftime("%Y%m%d%H%M%S", time.gmtime(entry.get("ts", 0)))
                date = "\033[0;7;36m\033[7m" + stamp[:4] + "\033[27m" + stamp[4:8] + "\033[0;1m" + stamp[8:10] + "\033[0;36m" + stamp[10:12] + "\033[0m" + stamp[12:14]
                size = entry.get("sz", 0)
                for unit in "BKMGTPE":
                    if size < 1024:
                        break
                    size /= 1024.0
                rendered = str(size)[:4].rstrip(".") + unit
                color = {"B": 6, "K": 5, "M": 1, "G": 3}.get(unit, 0)
                name = urllib.parse.unquote(entry["href"])
                lines.append(date + f"\033[0;3{color}m {rendered:>5}\033[0m " + ("\033[94m" if directory else "") + name)
        return self.send(200, ("\n".join(lines) + "\n\033[0m").encode(), {"Content-Type": "text/plain; charset=utf-8"})

    def do_GET(self):
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        expected = "Basic " + base64.b64encode(b"fixture-user:fixture-password").decode()
        if not getattr(self.server, "gui", False) and self.headers.get("Authorization") != expected:
            return self.send(401, b"credentials missing")
        if getattr(self.server, "gui", False):
            fixtures = pathlib.Path(__file__).parent / "fixtures"
            if path == "/":
                names = ["readme.txt", "markup.html", "colors.txt", "slow-headers.txt", "slow-stream.txt", "unauthorized", "truncated"]
                names += [p.name for p in sorted(fixtures.iterdir()) if p.suffix.lower() in {".png", ".jpg", ".bmp", ".ico", ".gif", ".webp", ".svg"}]
                body = {"dirs": [{"href": "folder/", "sz": 3*1024**3, "ts": 1700000000, "tags": {".files": 122}}, {"href": "sizes/", "sz": 54*1024**3, "ts": 1700000000, "tags": {".files": 8}}], "files": [{"href": urllib.parse.quote(name), "sz": (fixtures / name).stat().st_size if (fixtures / name).is_file() else 128, "ts": 1700000000} for name in names]}
                return self.listing(body)
            if path == "/folder/":
                names = ["café 文件.txt", "100% name?.txt"] + [f"item-{i:03}.txt" for i in range(120)]
                body = {"dirs": [], "files": [{"href": urllib.parse.quote(name), "sz": 32, "ts": 1700000000 + i} for i, name in enumerate(names)]}
                return self.listing(body)
            if path == "/sizes/":
                body = {"dirs": [
                    {"href": "bytes/", "sz": 303, "ts": 1700000000},
                    {"href": "kib/", "sz": 110821, "ts": 1700000000},
                    {"href": "mib/", "sz": 45590591, "ts": 1700000000},
                    {"href": "gib/", "sz": 58853872782, "ts": 1700000000},
                    {"href": "unknown/", "ts": 1700000000},
                ], "files": [
                    {"href": "zero.txt", "sz": 0, "ts": 1700000000},
                    {"href": "unknown.bin", "ts": 1700000000},
                    {"href": urllib.parse.quote("long-name-" + "data-"*40 + "preview.txt"), "sz": 12345, "ts": 1700000000},
                ]}
                return self.listing(body)
            if path.startswith("/sizes/"):
                if path.endswith("/"):
                    return self.listing({"dirs": [], "files": []})
                return self.send(200, b"Size display fixture.\n", {"Content-Type": "text/plain"})
            if path.startswith("/folder/"):
                return self.send(200, ("UTF-8 filename: " + path.rsplit("/", 1)[-1] + "\n").encode(), {"Content-Type": "text/plain; charset=utf-8"})
            if path == "/readme.txt":
                return self.send(200, b"Copyparty Viewer test directory\n\nClick files and folders; use Back to restore the directory.\nNetwork and preview work stays off the UI thread.\n", {"Content-Type": "text/plain"})
            if path == "/markup.html":
                return self.send(200, b"<!doctype html><html><body><script>alert('must not execute')</script><h1>Read-only source preview</h1></body></html>\n", {"Content-Type": "text/html"})
            if path == "/colors.txt":
                return self.send(200, b"Plain text\n\x1b[31mRed ANSI text\x1b[0m\n\x1b[1mBold text\x1b[0m\n", {"Content-Type": "text/plain"})
            if path == "/slow-headers.txt":
                time.sleep(8)
                return self.send(200, b"Headers arrived after eight seconds.\n", {"Content-Type": "text/plain"})
            if path == "/slow-stream.txt":
                path = "/slow"
            candidate = fixtures / path.lstrip("/")
            if candidate.parent == fixtures and candidate.is_file() and candidate.suffix.lower() in {".png", ".jpg", ".bmp", ".ico", ".gif", ".webp", ".svg"}:
                return self.send(200, candidate.read_bytes(), {"Content-Type": mimetypes.guess_type(candidate.name)[0] or "application/octet-stream"})
        if path == "/":
            return self.listing({"dirs": [{"href": "folder/", "sz": 0, "ts": 1700000000}], "files": [{"href": "file.txt", "sz": 16, "ts": 1700000000}]})
        if path == "/file.txt":
            return self.send(200, b"fixture payload\n", {"Content-Type": "text/plain"})
        if path == "/redirect":
            return self.send(302, headers={"Location": "/file.txt"})
        if path == "/redirect-terminal":
            return self.send(302, headers={"Location": "/?ls=v"})
        if path == "/foreign":
            return self.send(302, headers={"Location": "http://localhost:1/credential-leak"})
        if path == "/loop":
            return self.send(302, headers={"Location": "/loop?dl"})
        if path == "/query":
            return self.send(302, headers={"Location": "/file.txt?move=/changed"})
        if path == "/unauthorized":
            return self.send(401, b"not a file")
        if path == "/forbidden":
            return self.send(403, b"not a file")
        if path == "/huge":
            self.send_response(200)
            self.send_header("Content-Length", "2000000")
            self.end_headers()
            self.close_connection = True
            return
        if path == "/unbounded":
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            self.wfile.write(b"x" * 4096)
            return
        if path == "/truncated":
            self.send_response(200)
            self.send_header("Content-Length", "1000")
            self.end_headers()
            self.wfile.write(b"partial")
            self.close_connection = True
            return
        if path == "/slow":
            self.send_response(200)
            self.send_header("Content-Length", "10000000")
            self.end_headers()
            try:
                for _ in range(1000):
                    self.wfile.write(b"x" * 10000)
                    self.wfile.flush()
                    time.sleep(0.03)
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
            return
        if path == "/slow-headers":
            time.sleep(8)
            return self.send(200, b"late response")
        return self.send(404, b"missing")


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        pass


if __name__ == "__main__":
    serve = len(sys.argv) >= 2 and sys.argv[1] == "--serve"
    if not serve and len(sys.argv) != 2:
        raise SystemExit("Usage: http_fixture.py /path/to/core_tests  OR  http_fixture.py --serve [port]")
    port = int(sys.argv[2]) if serve and len(sys.argv) > 2 else 0
    server = Server(("127.0.0.1", port), Handler)
    server.gui = serve
    if serve:
        print(f"GUI fixture: http://127.0.0.1:{server.server_port}/ (no credentials)", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        raise SystemExit(0)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run([sys.argv[1], "--http-fixture", f"http://127.0.0.1:{server.server_port}/"], check=False)
    finally:
        server.shutdown()
    raise SystemExit(result.returncode)
