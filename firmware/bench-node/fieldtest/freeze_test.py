# Uploads a deliberate test build over WiFi (after the MAC check) and logs, once a second, the bench node's state
# and the Shelly's relay, until the Shelly has cut the bar or the time limit runs out. Used for the frozen bench node
# tests. Usage: python freeze_test.py <firmware.bin> <limit s> [baron]  ("baron": bar on first).
import base64, json, os, subprocess, sys, time, urllib.request
C = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")))  # gitignored
A = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
B = "http://" + C["bench_host"]; S = "http://" + C["shelly_host"]
BIN, LIMIT = sys.argv[1], float(sys.argv[2])
def st():
    try: return json.loads(urllib.request.urlopen(urllib.request.Request(B + "/api/state", headers={"Authorization": A}), timeout=2).read())
    except Exception: return None
def sw():
    try: return json.loads(urllib.request.urlopen(S + "/rpc/Switch.GetStatus?id=0", timeout=3).read())
    except Exception: return None
s = st()
assert s and s["mac"] == C["bench_mac"], "MAC check failed"
assert not s["warn"]["on"] and not s["alarm"]["on"] and not s["trial"], "not ready"
if len(sys.argv) > 3 and sys.argv[3] == "baron" and not s["bar"]["shelly"]:
    req = urllib.request.Request(B + "/api/act", data=b"a=bar.on", headers={"Authorization": A})
    print(time.strftime("%H:%M:%S"), "bar.on:", urllib.request.urlopen(req, timeout=8).read().decode(), flush=True)
    time.sleep(5); s = st()
print(time.strftime("%H:%M:%S"), "before:", s["mac"], s["fw"], s["commit"], s["phase"], "relay", s["bar"]["shelly"], flush=True)
r = subprocess.run(["curl", "-s", "-m", "120", "-u", C["user"] + ":" + C["password"], "-F", "fw=@" + BIN, B + "/api/ota"], capture_output=True, text=True)
print(time.strftime("%H:%M:%S"), "upload:", r.stdout.strip()[:120], flush=True)
t0 = time.time(); lastb = lasts = None
while time.time() - t0 < LIMIT:
    b = st(); w = sw()
    kb = b and (b["fw"], b["phase"], b["warn"]["why"] if b["warn"]["on"] else "", b["bar"]["link"], b["alarm"]["code"])
    ks = w and (w.get("output"), w.get("timer_started_at"), w.get("source"))
    if kb != lastb: print(time.strftime("%H:%M:%S"), "bench:", kb if b else "no answer", flush=True); lastb = kb
    if ks != lasts:
        ts = w and w.get("timer_started_at")
        print(time.strftime("%H:%M:%S"), "shelly:", ks, ts and time.strftime("%H:%M:%S", time.localtime(ts)), flush=True); lasts = ks
    if w and w.get("output") is False and time.time() - t0 > 40: break
    time.sleep(1)
print(time.strftime("%H:%M:%S"), "end", flush=True)
