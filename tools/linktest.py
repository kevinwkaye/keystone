"""Link test: start Keystone, connect like the phone does, stay connected, reconnect.

Run on the build machine after building:  python tools/linktest.py path/to/Keystone(.exe)
Fails if Keystone stops answering, drops a phone, or won't take a second one.
"""
import json, os, socket, subprocess, sys, time
from pathlib import Path

exe = sys.argv[1]
proc = subprocess.Popen([exe])
ok = True

def log_tail():
    p = Path.home() / "Documents" / "Keystone" / "keystone.log"
    if p.exists():
        print("---- keystone.log ----")
        print(p.read_text(errors="replace")[-3000:])

def fail(msg):
    global ok
    ok = False
    print("FAIL", msg)

def session(n, hold):
    s = socket.create_connection(("127.0.0.1", 47801), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    f = s.makefile("r", encoding="utf-8")
    s.sendall(b'{"t":"hello","client":"test"}\n')
    seen = {}
    end = time.time() + hold
    sent_set = False
    while time.time() < end:
        line = f.readline()
        if not line:
            fail(f"session {n}: Keystone closed the link after {hold - (end - time.time()):.1f} s, saw {sorted(seen)}")
            return
        m = json.loads(line)
        seen[m["t"]] = seen.get(m["t"], 0) + 1
        if "state" in seen and not sent_set:
            s.sendall(b'{"t":"set","id":"grains.mix","v":0.4}\n{"t":"ping","ts":1}\n')
            sent_set = True
    print(f"session {n}: held {hold} s, messages {seen}")
    for t in ("schema", "state", "meter", "pong"):
        if t not in seen:
            fail(f"session {n}: never got {t}")
    s.close()

try:
    time.sleep(4)
    # discovery probe
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.settimeout(3)
    u.sendto(b"KEYSTONE?", ("127.0.0.1", 47800))
    try:
        print("probe reply", u.recvfrom(512)[0].decode())
    except Exception as e:
        fail(f"no probe reply ({e})")
    # what the phone's address sweep does: connect and close at once
    socket.create_connection(("127.0.0.1", 47801), timeout=3).close()
    time.sleep(0.5)
    session(1, 8)
    time.sleep(1)
    session(2, 5)   # the phone coming back
    if proc.poll() is not None:
        fail(f"Keystone exited with code {proc.returncode}")
except Exception as e:
    fail(f"exception {e!r}")
finally:
    log_tail()
    if proc.poll() is None:
        proc.kill()
sys.exit(0 if ok else 1)
