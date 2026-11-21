#!/usr/bin/env python3
"""Login service for the irrigation dashboard (Python standard library only).

Runs on 127.0.0.1 behind Caddy:
  POST /api/login     {"username","password"}         -> sets session cookie
  POST /api/logout                                     -> clears session cookie
  GET  /api/me                                         -> {"username","must_change"} or 401
  POST /api/password  {"current","new"}                -> changes password, new cookie
  GET  /api/auth      (Caddy forward_auth for /mqtt)   -> 204 or 401

Passwords are stored as salted scrypt hashes in users.json (mode 600).
Sessions are HMAC-signed cookies tied to the user's current password hash, so
changing a password logs out every other session.

User management (run as root):
  auth.py adduser <name> [--must-change]   (password from IRR_NEW_PASSWORD or prompt)
  auth.py passwd <name> [--must-change]
  auth.py deluser <name>
  auth.py list
"""
import base64
import getpass
import hashlib
import hmac
import json
import os
import secrets
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE_DIR = os.environ.get("IRR_STATE_DIR", "/var/lib/irrigation")
USERS_FILE = os.path.join(STATE_DIR, "users.json")
KEY_FILE = os.path.join(STATE_DIR, "session.key")
PUBLIC_HOST = os.environ.get("IRR_HOST", "")          # e.g. irrigation.example.com
PORT = int(os.environ.get("IRR_PORT", "8090"))

COOKIE = "irr_session"
SESSION_TTL = 30 * 24 * 3600                          # 30 days
MIN_PASSWORD = 8
MAX_PASSWORD = 256
SCRYPT_N, SCRYPT_R, SCRYPT_P = 2 ** 15, 8, 1          # ~32 MB per hash
SCRYPT_MAXMEM = 64 * 1024 * 1024
LOGIN_MAX_FAILS = 10                                  # per IP ...
LOGIN_WINDOW = 15 * 60                                # ... per 15 minutes

_lock = threading.Lock()
_fails = {}                                           # ip -> [timestamps]


# ---------------------------------------------------------------- storage
def b64e(b):
    return base64.urlsafe_b64encode(b).decode().rstrip("=")


def b64d(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def load_users():
    try:
        with open(USERS_FILE, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


def save_users(users):
    tmp = USERS_FILE + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(users, f, indent=2)
    os.replace(tmp, USERS_FILE)


def session_key():
    try:
        with open(KEY_FILE, "rb") as f:
            return f.read()
    except FileNotFoundError:
        key = secrets.token_bytes(32)
        fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as f:
            f.write(key)
        return key


# ---------------------------------------------------------------- passwords
def hash_password(password):
    salt = secrets.token_bytes(16)
    dk = hashlib.scrypt(password.encode(), salt=salt, n=SCRYPT_N, r=SCRYPT_R, p=SCRYPT_P,
                        maxmem=SCRYPT_MAXMEM, dklen=32)
    return f"scrypt${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}${b64e(salt)}${b64e(dk)}"


def verify_password(password, stored):
    try:
        _, n, r, p, salt, dk = stored.split("$")
        calc = hashlib.scrypt(password.encode(), salt=b64d(salt), n=int(n), r=int(r), p=int(p),
                              maxmem=SCRYPT_MAXMEM, dklen=len(b64d(dk)))
        return hmac.compare_digest(calc, b64d(dk))
    except (ValueError, TypeError):
        return False


_DUMMY_HASH = None


def dummy_verify(password):
    """Spend the same time as a real check, so unknown usernames aren't revealed by timing."""
    global _DUMMY_HASH
    if _DUMMY_HASH is None:
        _DUMMY_HASH = hash_password(secrets.token_hex(8))
    verify_password(password, _DUMMY_HASH)


def password_problem(pw):
    if not isinstance(pw, str) or len(pw) < MIN_PASSWORD:
        return f"Password must be at least {MIN_PASSWORD} characters"
    if len(pw) > MAX_PASSWORD:
        return "Password is too long"
    return None


# ---------------------------------------------------------------- sessions
def pw_version(user_rec):
    return hashlib.sha256(user_rec["hash"].encode()).hexdigest()[:16]


def make_session(username, user_rec):
    payload = f"{username}|{int(time.time()) + SESSION_TTL}|{pw_version(user_rec)}"
    sig = hmac.new(session_key(), payload.encode(), hashlib.sha256).digest()
    return f"{b64e(payload.encode())}.{b64e(sig)}"


def read_session(cookie_header):
    """Return (username, user_rec) for a valid session cookie, else (None, None)."""
    if not cookie_header:
        return None, None
    token = None
    for part in cookie_header.split(";"):
        k, _, v = part.strip().partition("=")
        if k == COOKIE:
            token = v
    if not token or "." not in token:
        return None, None
    try:
        p64, s64 = token.split(".", 1)
        payload = b64d(p64)
        expected = hmac.new(session_key(), payload, hashlib.sha256).digest()
        if not hmac.compare_digest(expected, b64d(s64)):
            return None, None
        username, exp, ver = payload.decode().split("|")
        if int(exp) < time.time():
            return None, None
    except (ValueError, TypeError, UnicodeDecodeError):
        return None, None
    rec = load_users().get(username)
    if not rec or not hmac.compare_digest(pw_version(rec), ver):
        return None, None
    return username, rec


# ---------------------------------------------------------------- rate limiting
def too_many_fails(ip):
    now = time.time()
    with _lock:
        recent = [t for t in _fails.get(ip, []) if now - t < LOGIN_WINDOW]
        _fails[ip] = recent
        return len(recent) >= LOGIN_MAX_FAILS


def record_fail(ip):
    with _lock:
        _fails.setdefault(ip, []).append(time.time())


# ---------------------------------------------------------------- HTTP
class Handler(BaseHTTPRequestHandler):
    server_version = "irrigation-auth"
    sys_version = ""

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.client_ip(), fmt % args))

    def client_ip(self):
        xff = self.headers.get("X-Forwarded-For", "")
        return xff.split(",")[-1].strip() if xff else self.client_address[0]

    def origin_ok(self):
        origin = self.headers.get("Origin")
        return not origin or not PUBLIC_HOST or origin == f"https://{PUBLIC_HOST}"

    def send_json(self, code, obj, cookie=None):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        if cookie is not None:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        self.wfile.write(body)

    def session_cookie(self, value, max_age):
        return f"{COOKIE}={value}; Path=/; Max-Age={max_age}; HttpOnly; Secure; SameSite=Strict"

    def read_json(self):
        if "application/json" not in self.headers.get("Content-Type", ""):
            return None
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > 4096:
            return None
        try:
            data = json.loads(self.rfile.read(length))
            return data if isinstance(data, dict) else None
        except ValueError:
            return None

    # GET
    def do_GET(self):
        if self.path == "/api/auth":
            user, rec = read_session(self.headers.get("Cookie"))
            ok = user and not rec.get("must_change") and self.origin_ok()
            self.send_response(204 if ok else 401)
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/api/me":
            user, rec = read_session(self.headers.get("Cookie"))
            if user:
                self.send_json(200, {"username": user, "must_change": bool(rec.get("must_change"))})
            else:
                self.send_json(401, {"error": "Not logged in"})
        else:
            self.send_json(404, {"error": "Not found"})

    # POST
    def do_POST(self):
        if not self.origin_ok():
            return self.send_json(403, {"error": "Bad origin"})
        if self.path == "/api/logout":
            return self.send_json(200, {"ok": True}, cookie=self.session_cookie("", 0))
        data = self.read_json()
        if data is None:
            return self.send_json(400, {"error": "Bad request"})
        if self.path == "/api/login":
            self.login(data)
        elif self.path == "/api/password":
            self.change_password(data)
        else:
            self.send_json(404, {"error": "Not found"})

    def login(self, data):
        ip = self.client_ip()
        if too_many_fails(ip):
            return self.send_json(429, {"error": "Too many attempts, try again in 15 minutes"})
        username = str(data.get("username", "")).strip()
        password = data.get("password", "")
        rec = load_users().get(username)
        if not isinstance(password, str) or len(password) > MAX_PASSWORD:
            password = ""
        if rec is None:
            dummy_verify(password)
            ok = False
        else:
            ok = verify_password(password, rec["hash"])
        if not ok:
            record_fail(ip)
            return self.send_json(401, {"error": "Wrong username or password"})
        self.send_json(200, {"username": username, "must_change": bool(rec.get("must_change"))},
                       cookie=self.session_cookie(make_session(username, rec), SESSION_TTL))

    def change_password(self, data):
        username, rec = read_session(self.headers.get("Cookie"))
        if not username:
            return self.send_json(401, {"error": "Not logged in"})
        ip = self.client_ip()
        if too_many_fails(ip):
            return self.send_json(429, {"error": "Too many attempts, try again in 15 minutes"})
        current, new = data.get("current", ""), data.get("new", "")
        if not isinstance(current, str) or not verify_password(current, rec["hash"]):
            record_fail(ip)
            return self.send_json(403, {"error": "Current password is wrong"})
        problem = password_problem(new)
        if problem:
            return self.send_json(400, {"error": problem})
        if new == current:
            return self.send_json(400, {"error": "New password must be different"})
        with _lock:
            users = load_users()
            users[username]["hash"] = hash_password(new)
            users[username]["must_change"] = False
            users[username]["changed"] = int(time.time())
            save_users(users)
            rec = users[username]
        self.send_json(200, {"username": username, "must_change": False},
                       cookie=self.session_cookie(make_session(username, rec), SESSION_TTL))


# ---------------------------------------------------------------- CLI
def ask_password():
    pw = os.environ.get("IRR_NEW_PASSWORD")
    if pw is None:
        pw = getpass.getpass("New password: ")
        if getpass.getpass("Repeat: ") != pw:
            sys.exit("Passwords do not match")
    problem = password_problem(pw)
    if problem:
        sys.exit(problem)
    return pw


def main(argv):
    os.makedirs(STATE_DIR, exist_ok=True)
    cmd = argv[1] if len(argv) > 1 else "serve"
    if cmd == "serve":
        session_key()
        server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
        print(f"irrigation-auth listening on 127.0.0.1:{PORT}", flush=True)
        server.serve_forever()
    elif cmd in ("adduser", "passwd") and len(argv) >= 3:
        name = argv[2]
        if not name or not all(c.isalnum() or c in "._-" for c in name):
            sys.exit("Username may only contain letters, digits, . _ -")
        with _lock:
            users = load_users()
            if cmd == "adduser" and name in users:
                sys.exit(f"User {name} already exists")
            if cmd == "passwd" and name not in users:
                sys.exit(f"No such user {name}")
            users[name] = {"hash": hash_password(ask_password()),
                           "must_change": "--must-change" in argv,
                           "changed": int(time.time())}
            save_users(users)
        print(f"{'Added' if cmd == 'adduser' else 'Updated'} {name}"
              + (" (must change password at next login)" if "--must-change" in argv else ""))
    elif cmd == "deluser" and len(argv) >= 3:
        with _lock:
            users = load_users()
            if users.pop(argv[2], None) is None:
                sys.exit(f"No such user {argv[2]}")
            save_users(users)
        print(f"Deleted {argv[2]}")
    elif cmd == "list":
        for name, rec in sorted(load_users().items()):
            print(name + ("  (must change password)" if rec.get("must_change") else ""))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
