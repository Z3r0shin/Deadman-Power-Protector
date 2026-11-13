# Failure tests on the real bench node and Shelly. Logs every second; prints one result line per test.
import base64, json, sys, time, urllib.parse, urllib.request

import json, os
_C = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")))  # gitignored
B = "http://" + _C["bench_host"]
S = "http://" + _C["shelly_host"]
AUTH = "Basic " + base64.b64encode((_C["user"] + ":" + _C["password"]).encode()).decode()
os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
LOG = open(sys.argv[1], "w", buffering=1)
T0 = time.time()


def get(url, auth=False, timeout=6):
    req = urllib.request.Request(url, headers={"Authorization": AUTH} if auth else {})
    return json.loads(urllib.request.urlopen(req, timeout=timeout).read())


def post(url, data, timeout=8):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode(), headers={"Authorization": AUTH})
    return json.loads(urllib.request.urlopen(req, timeout=timeout).read())


def rpc(method, params=None):
    req = urllib.request.Request(S + "/rpc", data=json.dumps({"id": 1, "method": method, "params": params or {}}).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=8).read()).get("result")


def lease_id():
    """The lease script's id on the Shelly, by its name."""
    for sc in rpc("Script.List")["scripts"]:
        if sc["name"] == "lease":
            return sc["id"]
    raise SystemExit("no script named lease on the Shelly")


def act(a, v=""):
    return post(B + "/api/act", {"a": a, "v": v})


def snap():
    row = {"t": round(time.time() - T0, 1)}
    try:
        s = get(B + "/api/state", auth=True)
        row.update(phase=s["phase"], warn=s["warn"]["on"], why=s["warn"]["why"], wleft=s["warn"]["left"],
                   shelly=s["bar"]["shelly"], sense=s["bar"]["sense"], link=s["bar"]["link"], lease=s["bar"]["lease"],
                   ceil=s["bar"]["ceil"], due=s["away"]["due"], seen=s["away"]["seen"], alarm=s["alarm"]["code"],
                   up=s["up"])
    except Exception as e:
        row["bench"] = "no answer: %s" % e.__class__.__name__
    try:
        sw = rpc("Switch.GetStatus", {"id": 0})
        row["relay"] = sw["output"]
        row["timer"] = sw.get("timer_duration")
    except Exception as e:
        row["relay"] = "no answer"
    LOG.write(json.dumps(row) + "\n")
    return row


def watch(seconds, stop=None, label=""):
    """Snapshot every second for up to `seconds`; returns (rows, stopping row or None)."""
    rows = []
    end = time.time() + seconds
    while time.time() < end:
        r = snap()
        rows.append(r)
        if stop and stop(r):
            return rows, r
        time.sleep(1)
    return rows, None


def first(rows, cond):
    for r in rows:
        if cond(r):
            return r
    return None


def say(msg):
    line = "[%7.1f s] %s" % (time.time() - T0, msg)
    print(line, flush=True)
    LOG.write("# " + line + "\n")


def wait_on():
    rows, r = watch(30, lambda r: r.get("relay") is True and r.get("phase") in ("on", "hold", "warning"))
    return r


# The right devices, by their MACs (config.json), before anything is switched.
if get(B + "/api/state", auth=True)["mac"] != _C["bench_mac"] or rpc("Shelly.GetDeviceInfo")["mac"] != _C["shelly_mac"]:
    raise SystemExit("wrong device: the MACs do not match config.json")
LEASE_ID = lease_id()

try:
    # 0. Radar calibration in the empty room.
    say("T0 radar calibration: %s" % act("radar.cal", "5"))
    rows, r = watch(90, lambda r: False)
    st = get(B + "/api/state", auth=True)["radar"]
    say("T0 result: stage=%s max_m=%s max_s=%s thresholds now m=%s s=%s" % (
        st["cal"]["stage"], st["cal"]["max_m"], st["cal"]["max_s"], st["par"]["sm"], st["par"]["ss"]))

    # 1. Shelly script restart while on: 600 s ceiling, the bench warns, nobody answers, cut.
    say("T1 restart ceiling: waiting for the cut (bar was on when the script started)")
    rows, r = watch(900, lambda r: r.get("relay") is False)
    w = first(rows, lambda r: r.get("warn") == 1)
    sd = None
    if r:
        rows2, sd = watch(30, lambda r: r.get("sense") == 0)
    say("T1 result: warning seen at t=%s (why=%s, left=%s s); relay off at t=%s; bench phase then=%s; bar sense dead at t=%s" % (
        w and w["t"], w and w.get("why"), w and w.get("wleft"), r and r["t"], r and r.get("phase"), sd and sd["t"]))

    # 9. Junk on the lease endpoint while off: nothing turns on.
    junk = ["op=renew&l=abc", "op=renew&l=-1", "op=renew&l=99999999999", "op=", "op=renew&&&", "x=1", "op=status&press=1"]
    res = []
    for q in junk:
        try:
            a = json.loads(urllib.request.urlopen(S + "/script/%d/lease?" % LEASE_ID + q, timeout=5).read())
            res.append("%s -> ok=%s why=%s on=%s" % (q, a["ok"], a["why"], a["on"]))
        except Exception as e:
            res.append("%s -> %s" % (q, e))
    sw = rpc("Switch.GetStatus", {"id": 0})
    say("T9 result: relay=%s after junk; %s" % (sw["output"], " | ".join(res)))

    # 2. Script stopped while on: the bench warns "no link", the Shelly's own timer cuts.
    say("T2 script stopped: bar.on -> %s" % act("bar.on"))
    wait_on()
    rows, r = watch(40, lambda r: False)
    t_stop = time.time() - T0
    say("T2 Script.Stop -> %s" % rpc("Script.Stop", {"id": LEASE_ID}))
    rows, r = watch(420, lambda r: r.get("relay") is False)
    lw = first(rows, lambda r: r.get("why") == "link")
    say("T2 result: script stopped at t=%.1f; link warning at t=%s; relay off at t=%s (%.0f s after the stop); timer at stop=%s" % (
        t_stop, lw and lw["t"], r and r["t"], (r["t"] - t_stop) if r else -1, rows[0].get("timer")))
    say("T2 Script.Start -> %s" % rpc("Script.Start", {"id": LEASE_ID}))
    watch(15)

    # 6+8. Absence in an empty room, with a bench node restart in the middle.
    say("T6 absence: bar.on -> %s" % act("bar.on"))
    on = wait_on()
    t_on = on["t"] if on else time.time() - T0
    rows, r = watch(600, lambda r: r.get("relay") is False)
    before = rows[-1]
    say("T8 bench restart at t=%.1f (due was %s s) -> %s" % (time.time() - T0, before.get("due"), post(B + "/api/reboot", {})))
    rows_b, r = watch(60, lambda r: r.get("phase") in ("on", "warning") and r.get("up", 99) < 60)
    say("T8 result: after restart phase=%s due=%s s (was %s s, 0-60 s earlier)" % (r and r.get("phase"), r and r.get("due"), before.get("due")))
    rows, r = watch(900, lambda r: r.get("relay") is False)
    w = first(rows_b + rows, lambda r: r.get("why") == "absence")
    say("T6 result: on at t=%.1f; absence warning at t=%s (left %s s); relay off at t=%s (%.1f min after on)" % (
        t_on, w and w["t"], w and w.get("wleft"), r and r["t"], ((r["t"] - t_on) / 60) if r else -1))

    # 5. Shelly reboot while on.
    say("T5 Shelly reboot: bar.on -> %s" % act("bar.on"))
    wait_on()
    watch(40)
    say("T5 Shelly.Reboot -> %s" % rpc("Shelly.Reboot"))
    time.sleep(20)
    rows, r = watch(60, lambda r: r.get("relay") in (True, False))
    say("T5 after reboot: relay=%s phase=%s ceil=%s" % (r and r.get("relay"), r and r.get("phase"), r and r.get("ceil")))
    if r and r.get("relay") is True:
        rows, r2 = watch(800, lambda r: r.get("relay") is False)
        w = first(rows, lambda r: r.get("warn") == 1)
        say("T5 result: warning at t=%s (why=%s); relay off at t=%s" % (w and w["t"], w and w.get("why"), r2 and r2["t"]))
    else:
        say("T5 result: the relay came back OFF after the reboot")

    say("END: relay=%s" % rpc("Switch.GetStatus", {"id": 0})["output"])
except Exception as e:
    say("ABORTED: %r" % e)
    try:
        say("relay now %s" % rpc("Switch.GetStatus", {"id": 0})["output"])
    except Exception:
        pass
