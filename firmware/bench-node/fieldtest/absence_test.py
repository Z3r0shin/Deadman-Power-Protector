# Absence test on the real devices, with nobody at the bench:
#   1. bar off: log "seen" for a set time (default 15 min) - nobody must be counted;
#   2. bar on (from the web page): log until the cut, or a time limit (default 30 min):
#      when the absence warning starts, when the relay goes off, and any "seen" on the way.
# Usage: python absence_test.py [quiet_minutes] [limit_minutes]. Log: logs/absence_test_<date_time>.jsonl.
import base64, json, os, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
C = json.load(open(os.path.join(HERE, "config.json")))
A = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
B = "http://" + C["bench_host"]
QUIET = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
LIMIT = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0


def req(path, data=None, t=5):
    r = urllib.request.Request(B + path, data=data, headers={"Authorization": A})
    return json.loads(urllib.request.urlopen(r, timeout=t).read())


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
log = open(os.path.join(HERE, "logs", time.strftime("absence_test_%Y-%m-%d_%H%M.jsonl")), "w", buffering=1)


def sample(stage):
    s = req("/api/state")
    r = s["radar"]
    row = {"t": time.strftime("%H:%M:%S"), "stage": stage, "seen": s["away"]["seen"], "ago": s["away"]["ago"],
           "due": s["away"]["due"], "phase": s["phase"], "relay": s["bar"]["shelly"], "sense": s["bar"]["sense"],
           "warn": s["warn"]["on"], "why": s["warn"]["why"], "wleft": s["warn"]["left"], "st": r.get("st"),
           "md": r.get("md"), "me": r.get("me"), "sd": r.get("sd"), "se": r.get("se"), "fps": r.get("fps"),
           "alarm": s["alarm"]["code"], "cam": s.get("cam")}
    log.write(json.dumps(row) + "\n")
    return row


s = req("/api/state")
say("start", s["fw"], "commit", s.get("commit"), "phase", s["phase"])

# 1. Bar off, nobody counted.
t0, n, seen, raw, errors = time.time(), 0, 0, 0, 0
while time.time() - t0 < QUIET * 60:
    try:
        r = sample("off")
        n += 1; seen += r["seen"]; raw += 1 if r["st"] else 0
        if r["seen"]: say("SEEN while off:", json.dumps(r))
    except Exception:
        errors += 1
    time.sleep(1)
say("bar off %.0f min: seen in %d of %d samples; raw radar target in %d; errors %d" % (QUIET, seen, n, raw, errors))

# 2. Bar on, wait for the warning and the cut.
say("bar on:", req("/api/act", urllib.parse.urlencode({"a": "bar.on"}).encode()))
t1 = time.time()
on_at = warn_at = off_at = None
seen_on = 0
while time.time() - t1 < LIMIT * 60:
    try:
        r = sample("on")
    except Exception:
        time.sleep(1); continue
    el = time.time() - t1
    if r["relay"] == 1 and on_at is None: on_at = el; say("relay on")
    if r["seen"]: seen_on += 1; say("SEEN while on:", json.dumps(r))
    if r["warn"] and warn_at is None: warn_at = el; say("warning started:", r["why"], "left", r["wleft"], "s")
    if on_at is not None and r["relay"] == 0: off_at = el; say("relay off (cut)"); break
    time.sleep(1)
fmt = lambda v: "none" if v is None else "%.0f s (%.1f min)" % (v, v / 60)
say("result: relay on at %s, warning at %s, cut at %s after the bar went on; seen while on: %d samples"
    % (fmt(on_at), fmt(warn_at), fmt(off_at), seen_on))
if off_at is None:                               # no cut: put the bar back off, the safe state
    say("no cut within the limit: bar off:", req("/api/act", urllib.parse.urlencode({"a": "bar.off"}).encode()))
time.sleep(12)
s = req("/api/state")
say("after:", "phase", s["phase"], "relay", s["bar"]["shelly"], "sense", s["bar"]["sense"], "alarm", s["alarm"]["code"])
