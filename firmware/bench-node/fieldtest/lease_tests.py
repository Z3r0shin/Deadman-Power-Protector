# Lease script install and the tests R1, R3 and R5, on the real bench node and Shelly.
# R1: the script started with the relay already on (installing it is that start): the 600 s restart ceiling warns,
#     then cuts. R3: the script stopped with the bar on: the relay's own timer cuts within the lease.
# R5: the Shelly rebooted with the bar on: the relay stays on, and the 600 s restart ceiling warns, then cuts.
# Usage: python lease_tests.py <log file>. Nobody may press a key at the bench while it runs.
import base64, hashlib, json, os, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
C = json.load(open(os.path.join(HERE, "config.json")))  # gitignored
B = "http://" + C["bench_host"]
S = "http://" + C["shelly_host"]
AUTH = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
LEASE = os.path.join(HERE, "..", "..", "shelly-lease", "lease.js")
BENCH_MAC, SHELLY_MAC = C["bench_mac"], C["shelly_mac"]   # config.json, never committed
os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
LOG = open(sys.argv[1], "w", buffering=1)
T0 = time.time()


def get(url, auth=False, timeout=6):
    req = urllib.request.Request(url, headers={"Authorization": AUTH} if auth else {})
    return json.loads(urllib.request.urlopen(req, timeout=timeout).read())


def act(a, v=""):
    req = urllib.request.Request(B + "/api/act", data=urllib.parse.urlencode({"a": a, "v": v}).encode(),
                                 headers={"Authorization": AUTH})
    return json.loads(urllib.request.urlopen(req, timeout=8).read())


def rpc(method, params=None, timeout=10):
    req = urllib.request.Request(S + "/rpc", data=json.dumps({"id": 1, "method": method, "params": params or {}}).encode(),
                                 headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=timeout).read())
    if "error" in r:
        raise RuntimeError("%s: %s" % (method, r["error"]))
    return r.get("result")


def say(msg):
    line = "%s [%7.1f s] %s" % (time.strftime("%H:%M:%S"), time.time() - T0, msg)
    print(line, flush=True)
    LOG.write(line + "\n")


def snap():
    row = {"t": round(time.time() - T0, 1), "clock": time.strftime("%H:%M:%S")}
    try:
        s = get(B + "/api/state", auth=True)
        row.update(phase=s["phase"], warn=s["warn"]["on"], why=s["warn"]["why"], wleft=s["warn"]["left"],
                   shelly=s["bar"]["shelly"], sense=s["bar"]["sense"], link=s["bar"]["link"], lease=s["bar"]["lease"],
                   ceil=s["bar"]["ceil"], alarm=s["alarm"]["code"], up=s["up"])
    except Exception as e:
        row["bench_err"] = str(e)[:80]
    try:
        sw = rpc("Switch.GetStatus", {"id": 0}, timeout=4)
        row.update(relay=sw.get("output"), timer=sw.get("timer_duration"), source=sw.get("source"))
    except Exception as e:
        row["shelly_err"] = str(e)[:80]
    LOG.write(json.dumps(row) + "\n")
    return row


def watch(seconds, stop=None):
    rows, end = [], time.time() + seconds
    while time.time() < end:
        r = snap()
        rows.append(r)
        if stop and stop(r):
            break
        time.sleep(1)
    return rows


def first(rows, cond):
    for r in rows:
        if cond(r):
            return r
    return None


def lease_id():
    """The lease script's id on the Shelly, by its name."""
    for sc in rpc("Script.List")["scripts"]:
        if sc["name"] == "lease":
            return sc["id"]
    raise SystemExit("no script named lease on the Shelly")


def bar_on():
    say("bar.on -> %s" % act("bar.on"))
    rows = watch(20, lambda r: r.get("relay") is True and r.get("phase") == "on")
    r = rows[-1]
    if r.get("relay") is not True:
        raise SystemExit("the bar did not come on: %s" % r)
    return r


def install():
    code = open(LEASE, "rb").read()
    assert all(b < 128 for b in code), "lease.js must be ASCII"
    md5 = hashlib.md5(code).hexdigest()
    say("installing lease.js: %d bytes, MD5 %s" % (len(code), md5))
    say("Script.Stop -> %s" % rpc("Script.Stop", {"id": LEASE_ID}))
    text = code.decode("ascii")
    for i in range(0, len(text), 2048):
        rpc("Script.PutCode", {"id": LEASE_ID, "code": text[i:i + 2048], "append": i > 0})
    back, off = "", 0
    while True:
        r = rpc("Script.GetCode", {"id": LEASE_ID, "offset": off, "len": 4096})
        back += r["data"]
        off += len(r["data"])
        if not r.get("left"):
            break
    got = hashlib.md5(back.encode("ascii")).hexdigest()
    say("read back: %d bytes, MD5 %s, %s" % (len(back), got, "identical" if got == md5 else "DIFFERENT"))
    if got != md5:
        raise SystemExit("read-back differs: not started")
    say("Script.Start -> %s" % rpc("Script.Start", {"id": LEASE_ID}))
    time.sleep(2)
    say("Script.List -> %s" % rpc("Script.List"))


def ceiling_cut(label, limit_s):
    rows = watch(limit_s, lambda r: r.get("relay") is False)
    w = first(rows, lambda r: r.get("warn") and r.get("why") == "ceiling")
    off = first(rows, lambda r: r.get("relay") is False)
    say("%s result: warning (ceiling) at %s, left %s s; relay off at %s (source %s); alarm %s" % (
        label, w and w["clock"], w and w.get("wleft"), off and off["clock"], off and off.get("source"),
        rows[-1].get("alarm")))
    return rows


def main():
    s = get(B + "/api/state", auth=True)
    d = rpc("Shelly.GetDeviceInfo")
    say("bench node %s %s %s, phase %s; Shelly %s fw %s" % (s["mac"], s["fw"], s["commit"], s["phase"], d["mac"], d["ver"]))
    if s["mac"] != BENCH_MAC or d["mac"] != SHELLY_MAC:
        raise SystemExit("wrong device")
    if s["warn"]["on"] or s["alarm"]["on"]:
        raise SystemExit("a warning or alarm runs: not starting")
    global LEASE_ID
    LEASE_ID = lease_id()

    # R1: the bar on, then the new script started: the restart ceiling.
    r = bar_on()
    say("R1 bar on: ceil %s s, lease %s s" % (r.get("ceil"), r.get("lease")))
    install()
    time.sleep(35)
    r = snap()
    say("R1 after the start: relay %s phase %s ceil %s s" % (r.get("relay"), r.get("phase"), r.get("ceil")))
    ceiling_cut("R1", 700)
    watch(15)

    # R3: the script stopped with the bar on: the relay's own timer cuts.
    r = bar_on()
    say("R3 bar on: ceil %s s, timer %s s" % (r.get("ceil"), r.get("timer")))
    t_stop = time.strftime("%H:%M:%S")
    say("R3 Script.Stop -> %s" % rpc("Script.Stop", {"id": LEASE_ID}))
    rows = watch(360, lambda r: r.get("relay") is False)
    lw = first(rows, lambda r: r.get("warn") and r.get("why") == "link")
    off = first(rows, lambda r: r.get("relay") is False)
    say("R3 result: script stopped at %s; link warning at %s; relay off at %s (source %s)" % (
        t_stop, lw and lw["clock"], off and off["clock"], off and off.get("source")))
    say("R3 Script.Start -> %s" % rpc("Script.Start", {"id": LEASE_ID}))
    watch(40, lambda r: r.get("link") == "ok" and r.get("phase") == "off")

    # R5: the Shelly rebooted with the bar on.
    r = bar_on()
    say("R5 bar on: ceil %s s" % r.get("ceil"))
    say("R5 Shelly.Reboot -> %s" % rpc("Shelly.Reboot"))
    time.sleep(25)
    watch(60, lambda r: r.get("link") == "ok")
    r = snap()
    say("R5 after the reboot: relay %s phase %s ceil %s s" % (r.get("relay"), r.get("phase"), r.get("ceil")))
    if r.get("relay") is True:
        ceiling_cut("R5", 700)
    watch(15)
    r = snap()
    say("end: phase %s relay %s sense %s alarm %s; Script.List %s" % (
        r.get("phase"), r.get("relay"), r.get("sense"), r.get("alarm"), rpc("Script.List")))


if __name__ == "__main__":
    try:
        main()
    except SystemExit as e:
        say("STOPPED: %s" % e)
        raise
    except Exception as e:
        say("ERROR: %r" % e)
        raise
