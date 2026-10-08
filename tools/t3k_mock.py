#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
"""t3k_mock.py <port> <files dir> <expected client id> -- a stand-in for TONE3000's API (the parts the plugin uses),
strict where the real one is: client id, PKCE S256 (recomputes the challenge from the verifier), one-time codes,
redirect_uri match, Bearer auth on every resource and download. The "user" always picks tone 42 (captures) or 77
(IRs) on the authorize page. Serves files from <files dir>: a2.nam and ir.wav."""
import base64, hashlib, http.server, json, os, sys, urllib.parse
PORT, FILES, CLIENT = int(sys.argv[1]), sys.argv[2], sys.argv[3]
codes, TOKEN = {}, "tok-" + os.urandom(6).hex()
TONES = {"42": {"id": 42, "title": "Fender Twin: Clean/Edge", "user": {"username": "alice"}, "license": "cc-by",
                "url": "https://www.tone3000.com/tones/42", "gear": "amp", "format": "nam"},
         "77": {"id": 77, "title": "Green-Wood Catacombs", "user": {"username": "tone3000"}, "license": "t3k",
                "url": "https://www.tone3000.com/tones/77", "gear": "space", "format": "ir"}}
def b64u(b): return base64.urlsafe_b64encode(b).rstrip(b"=").decode()
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body=b"", ctype="application/json", headers=()):
        self.send_response(code)
        for k, v in headers: self.send_header(k, v)
        self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers()
        self.wfile.write(body)
    def authed(self):
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            self.send(401, b'{"error":"unauthorized"}'); return False
        return True
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = dict(urllib.parse.parse_qsl(u.query))
        if u.path == "/api/v1/oauth/authorize":
            need = {"client_id": CLIENT, "response_type": "code", "code_challenge_method": "S256", "prompt": "select_tone"}
            bad = [k for k, v in need.items() if q.get(k) != v] + [k for k in ("state", "code_challenge", "redirect_uri") if not q.get(k)]
            if q.get("format") == "nam" and q.get("architecture") != "2": bad.append("architecture")
            if q.get("format") == "ir" and q.get("gears") != "space_outboard_pedal_experimental": bad.append("gears")   # IRs only: spaces, outboard, pedals, experimental
            if bad: return self.send(400, json.dumps({"error": "invalid_request", "bad": bad}).encode())
            tone = "77" if q.get("format") == "ir" else "42"
            code = "code-" + os.urandom(6).hex()
            codes[code] = (q["code_challenge"], q["redirect_uri"])
            loc = q["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": q["state"], "tone_id": tone})
            return self.send(302, b"", "text/plain", [("Location", loc)])
        if not self.authed(): return
        if u.path.startswith("/api/v1/tones/"):
            t = TONES.get(u.path.rsplit("/", 1)[1])
            return self.send(200 if t else 404, json.dumps(t or {}).encode())
        if u.path == "/api/v1/models":
            base = "http://127.0.0.1:%d/files/" % PORT
            if q.get("tone_id") == "42":
                if q.get("architecture") != "2": return self.send(200, b'{"data":[]}')   # like the real API: A2 only on request
                data = [{"id": 1, "name": "Clean", "model_url": base + "1/a2.nam"}, {"id": 2, "name": "Edge / Crunch", "model_url": base + "2/a2.nam"}]
            else:
                data = [{"id": 3, "name": "Catacombs Far", "model_url": base + "3/ir.wav"}]
            return self.send(200, json.dumps({"data": data, "page": 1, "total": len(data)}).encode())
        if u.path.startswith("/files/"):
            return self.send(200, open(os.path.join(FILES, u.path.rsplit("/", 1)[1]), "rb").read(), "application/octet-stream")
        self.send(404, b"{}")
    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        f = dict(urllib.parse.parse_qsl(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()))
        if u.path != "/api/v1/oauth/token": return self.send(404, b"{}")
        c = codes.pop(f.get("code", ""), None)
        if not c or f.get("grant_type") != "authorization_code" or f.get("client_id") != CLIENT:
            return self.send(400, b'{"error":"invalid_grant"}')
        if b64u(hashlib.sha256(f.get("code_verifier", "").encode()).digest()) != c[0]:
            return self.send(400, b'{"error":"invalid_grant","why":"pkce"}')
        if f.get("redirect_uri") != c[1]: return self.send(400, b'{"error":"invalid_grant","why":"redirect_uri"}')
        self.send(200, json.dumps({"access_token": TOKEN, "refresh_token": "r", "token_type": "bearer", "expires_in": 3600}).encode())
http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
