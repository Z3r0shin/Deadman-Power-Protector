# Absence test #2 (with the radar range up to gate 3, which the Firmware Gospel rejects), then a detection log until morning. Prints a summary line per stage.
import base64, json, time, urllib.request

import json, os
_C = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")))  # gitignored
B = "http://" + _C["bench_host"]
S = "http://" + _C["shelly_host"]
A = "Basic " + base64.b64encode((_C["user"] + ":" + _C["password"]).encode()).decode()
os.makedirs(os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs"), exist_ok=True)
out = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs", "absence2.log"), "w", buffering=1)
T0 = time.time()


def state():
    return json.loads(urllib.request.urlopen(urllib.request.Request(B + "/api/state", headers={"Authorization": A}), timeout=6).read())


def act(a, v=""):
    d = ("a=%s&v=%s" % (a, v)).encode()
    return json.loads(urllib.request.urlopen(urllib.request.Request(B + "/api/act", data=d, headers={"Authorization": A}), timeout=10).read())


def relay():
    return json.loads(urllib.request.urlopen(S + "/rpc/Switch.GetStatus?id=0", timeout=6).read())["output"]


def say(m):
    print("[%s, %6.0f s] %s" % (time.strftime("%H:%M:%S"), time.time() - T0, m), flush=True)


def sample(seconds, stop_on_cut):
    detections, warn_at, cut_at, last_det = 0, None, None, None
    end = time.time() + seconds
    while time.time() < end:
        try:
            s = state()
            r = s["radar"]
            if r["st"] != 0:
                detections += 1
                out.write(json.dumps({"t": time.strftime("%H:%M:%S"), "st": r["st"], "md": r["md"], "me": r["me"],
                                      "sd": r["sd"], "se": r["se"], "dd": r["dd"], "gm": r["gm"], "gs": r["gs"],
                                      "out": r["out"]}) + "\n")
                last_det = time.strftime("%H:%M:%S")
            if stop_on_cut:
                if s["warn"]["on"] and warn_at is None:
                    warn_at = (time.time() - T0, s["warn"]["why"], s["warn"]["left"])
                if s["bar"]["shelly"] == 0 and relay() is False:
                    cut_at = time.time() - T0
                    break
        except Exception as e:
            out.write("# %r\n" % e)
        time.sleep(0.5)
    return detections, warn_at, cut_at, last_det


try:
    say("per-gate data on: %s" % act("radar.eng", "1"))
    say("bar ON for the absence test: %s" % act("bar.on"))
    time.sleep(3)
    say("relay %s" % relay())
    det, warn_at, cut_at, last = sample(40 * 60, True)
    say("ABSENCE TEST: detections while on=%d (last at %s); warning at %s; cut at %s s (%.1f min)" % (
        det, last, warn_at, cut_at, (cut_at / 60) if cut_at else -1))
    if not cut_at:
        say("no cut within 40 min: switching the bar OFF: %s" % act("bar.off"))
    det, _, _, last = sample(7 * 3600, False)
    say("NIGHT LOG: detections=%d (last at %s)" % (det, last))
    act("radar.eng", "0")
    say("END relay %s" % relay())
except Exception as e:
    say("ABORTED %r; relay %s" % (e, relay()))
