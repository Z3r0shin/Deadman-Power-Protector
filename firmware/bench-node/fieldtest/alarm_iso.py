# Isolates the alarm-test reboot: plays the alarm test (3 s) under given settings and reports a reboot.
import json, base64, urllib.request, urllib.parse, time, sys, os
C = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")))
A = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
B = "http://" + C["bench_host"]
def req(path, data=None, t=3):
    return json.loads(urllib.request.urlopen(urllib.request.Request(B + path, data=data, headers={"Authorization": A}), timeout=t).read())
def setv(k, v): return req("/api/set", urllib.parse.urlencode({"k": k, "v": v}).encode())
def wait_up():
    for _ in range(60):
        try: return req("/api/state?log=0")
        except Exception: time.sleep(0.5)
def run(label, settings, which=5):
    for k, v in settings.items(): print("  set", k, v, setv(k, v))
    time.sleep(1)
    up0 = req("/api/state?log=0")["up"]
    t0 = time.time(); print(label, "act", req("/api/act", ("a=buz.test&v=%d" % which).encode()))
    rebooted = None
    while time.time() - t0 < 8:
        try:
            s = req("/api/state?log=0", t=1)
            if s["up"] < up0: rebooted = time.time() - t0; break
        except Exception:
            pass
        time.sleep(0.25)
    s = wait_up()
    if s["up"] < up0 + 5: rebooted = rebooted or "yes"
    print(label, "->", "REBOOT" if rebooted else "no reboot", "| up", s["up"], "relay", s["bar"]["shelly"])
    time.sleep(2)
if __name__ == "__main__":
    for arg in sys.argv[1:]:
        label, rest = arg.split(":", 1)
        st = dict(kv.split("=") for kv in rest.split(",") if kv)
        run(label, st)
