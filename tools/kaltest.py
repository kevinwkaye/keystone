"""Kaleidoscope test: start Keystone, switch to Kaleidoscope like the phone does, load each
pedal it finds, move a control, put it away and go back to Keystone.

Run after building, with pedals in the Pedals folder beside Keystone (or in
Documents/Keystone/Pedals):  python tools/kaltest.py path/to/Keystone(.exe)
Fails if a pedal won't load, shows no controls, or Keystone stops answering.
"""
import json, shutil, socket, subprocess, sys, time
from pathlib import Path

# the phone's page layouts, so every control they place can be checked against the pedal
LAYOUTS = {}
_js = Path(__file__).resolve().parent.parent / "android/app/src/main/assets/pedal-layouts.js"
if shutil.which("node") and _js.exists():
    out = subprocess.run(["node", "-e", "global.window={};eval(require('fs').readFileSync(process.argv[1],'utf8'));"
                          "console.log(JSON.stringify(window.KS_PEDAL_LAYOUTS))", str(_js)], capture_output=True, text=True)
    if out.returncode == 0:
        LAYOUTS = json.loads(out.stdout)
if not LAYOUTS:
    print("note: node not found, the page layouts aren't checked")
    if __import__("os").environ.get("CI"):
        raise SystemExit("FAIL the build machine needs node to check the page layouts")
else:
    print(f"checking {len(LAYOUTS)} page layouts")


def layout_names(L):
    names = list(L.get("top", [])) + list(L.get("steps", [])) + list(L.get("hide", []))
    for row in L.get("rows", []):
        for c in row:
            if c.get("seg"):
                names.append(c["seg"])
            names += [k.split("|")[0] for k in c.get("knobs", [])]
    for f in L.get("feet", []):
        names += [f[k] for k in ("lit", "press", "text") if f.get(k)]
    for sec in L.get("sections", []):
        names += sec["params"]
    return names

exe = sys.argv[1]
proc = subprocess.Popen([exe, "--background"])
ok = True


def fail(msg):
    global ok
    ok = False
    print("FAIL", msg)


class Link:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", 47801), timeout=20)
        self.f = self.s.makefile("r", encoding="utf-8")
        self.state = None
        self.send({"t": "hello", "client": "kaltest"})

    def send(self, m):
        self.s.sendall((json.dumps(m) + "\n").encode())

    def wait(self, pred, secs=20):
        end = time.time() + secs
        while time.time() < end:
            line = self.f.readline()
            if not line:
                return None
            m = json.loads(line)
            if m.get("t") == "state":
                self.state = m
            if pred(m):
                return m
        return None


try:
    time.sleep(4)
    link = Link()
    st = link.wait(lambda m: m.get("t") == "state")
    if not st:
        raise SystemExit(fail("no state"))
    installed = st.get("kal", {}).get("installed", [])
    print("pedals installed:", installed)
    if not installed:
        fail("no pedals found")
    link.send({"t": "kal", "on": True})
    if not link.wait(lambda m: m.get("t") == "state" and m["kal"]["on"]):
        fail("Kaleidoscope didn't come up")
    for pid in installed:
        t0 = time.time()
        link.send({"t": "kalLoad", "id": pid})
        m = link.wait(lambda m: m.get("t") == "kalParams" and m.get("id") == pid, 60)
        if not m:
            fail(f"{pid}: didn't load")
            continue
        params = m["params"]
        kinds = {}
        for p in params:
            kinds[p["kind"]] = kinds.get(p["kind"], 0) + 1
        print(f"{pid}: loaded in {time.time() - t0:.1f} s, {len(params)} controls {kinds}")
        if not params:
            fail(f"{pid}: no controls")
            continue
        have = {p["name"] for p in params}
        missing = sorted({n for n in layout_names(LAYOUTS.get(pid, {})) if n not in have})
        if missing:
            fail(f"{pid}: its page places controls it doesn't have: {missing}")
        if pid in LAYOUTS:
            placed = set(layout_names(LAYOUTS[pid]))
            loose = [p["name"] for p in params if p["name"] not in placed]
            if loose:
                print(f"  {pid}: in Everything else: {loose}")
        slider = next((p for p in params if p["kind"] == "slider"), params[0])
        link.send({"t": "kalSet", "i": slider["i"], "v": 0.25})
        link.send({"t": "ping", "ts": 1})
        if not link.wait(lambda m: m.get("t") == "pong", 10):
            fail(f"{pid}: stopped answering after a control moved")
    link.send({"t": "kalLoad", "id": ""})
    if not link.wait(lambda m: m.get("t") == "kalParams" and m.get("id") == ""):
        fail("pedal wasn't put away")
    link.send({"t": "kal", "on": False, "rig": 0})
    if not link.wait(lambda m: m.get("t") == "state" and not m["kal"]["on"]):
        fail("didn't go back to Keystone")
    # gig lock keeps Kaleidoscope shut
    link.send({"t": "gigLock", "on": True})
    link.send({"t": "kal", "on": True})
    time.sleep(1)
    link.send({"t": "ping", "ts": 2})
    link.wait(lambda m: m.get("t") == "pong", 10)
    if link.state and link.state["kal"]["on"]:
        fail("Gig lock let Kaleidoscope in")
    link.send({"t": "gigLock", "on": False})
    link.send({"t": "ping", "ts": 3})
    link.wait(lambda m: m.get("t") == "pong", 10)
finally:
    proc.terminate()
    try:
        proc.wait(10)
    except Exception:
        proc.kill()
    if not ok:
        p = Path.home() / "Documents" / "Keystone" / "keystone.log"
        if p.exists():
            print(p.read_text(errors="replace")[-4000:])

print("OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
