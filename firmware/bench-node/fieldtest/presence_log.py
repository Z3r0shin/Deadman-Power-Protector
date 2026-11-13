# Logs what the presence sensor reports, twice a second, for a set time (default 15 min), with its per-gate data
# on; then prints a summary: when "seen" changed, the distances and energies while seen, and the highest energy per
# gate against the thresholds. Usage: python presence_log.py [minutes]
import base64, json, os, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
C = json.load(open(os.path.join(HERE, "config.json")))
A = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
B = "http://" + C["bench_host"]
MIN = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0


def req(path, data=None, t=4):
    r = urllib.request.Request(B + path, data=data, headers={"Authorization": A})
    return json.loads(urllib.request.urlopen(r, timeout=t).read())


def act(a, v=""):
    return req("/api/act", urllib.parse.urlencode({"a": a, "v": v}).encode())


os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
path = os.path.join(HERE, "logs", time.strftime("presence_%Y-%m-%d_%H%M.jsonl"))
out = open(path, "w", buffering=1)
print("per-gate data:", act("radar.eng", "1"), flush=True)
time.sleep(1)
s = req("/api/state")
par = s["radar"].get("par", {})
print("start", time.strftime("%H:%M:%S"), "seen", s["away"]["seen"], "params", json.dumps(par), flush=True)
t0 = time.time()
rows, errors = [], 0
while time.time() - t0 < MIN * 60:
    try:
        s = req("/api/state")
        r = s["radar"]
        row = {"t": time.strftime("%H:%M:%S"), "seen": s["away"]["seen"], "ago": s["away"]["ago"],
               "due": s["away"]["due"], "st": r.get("st"), "md": r.get("md"), "me": r.get("me"), "sd": r.get("sd"),
               "se": r.get("se"), "dd": r.get("dd"), "out": r.get("out"), "gm": r.get("gm"), "gs": r.get("gs"),
               "phase": s["phase"], "warn": s["warn"]["on"], "cam": s.get("cam")}
        out.write(json.dumps(row) + "\n")
        rows.append(row)
    except Exception:
        errors += 1
    time.sleep(0.5)
act("radar.eng", "0")

# Summary.
print("end", time.strftime("%H:%M:%S"), "samples", len(rows), "errors", errors, "log", os.path.basename(path))
prev = None
for r in rows:
    if r["seen"] != prev:
        print("  %s seen=%s st=%s md=%s me=%s sd=%s se=%s dd=%s out=%s" % (r["t"], r["seen"], r["st"], r["md"],
              r["me"], r["sd"], r["se"], r["dd"], r["out"]))
        prev = r["seen"]
seen = [r for r in rows if r["st"]]
print("  target reported in %d of %d samples; seen=1 in %d" % (len(seen), len(rows), sum(r["seen"] for r in rows)))
if seen:
    st = {}
    for r in seen: st[r["st"]] = st.get(r["st"], 0) + 1
    print("  target state counts (1 moving, 2 still, 3 both):", st)
    for k in ("md", "sd", "dd"):
        v = [r[k] for r in seen if r[k] is not None]
        if v: print("  %s cm: min %d max %d" % (k, min(v), max(v)))
gm = [max((r["gm"] or [0] * 9)[g] for r in rows if r["gm"]) for g in range(9)] if any(r["gm"] for r in rows) else []
gs = [max((r["gs"] or [0] * 9)[g] for r in rows if r["gs"]) for g in range(9)] if any(r["gs"] for r in rows) else []
print("  max moving energy per gate:", gm, " thresholds:", par.get("sm"))
print("  max still energy per gate: ", gs, " thresholds:", par.get("ss"))
print("  sensor max gates: moving", par.get("mg"), "still", par.get("sg"), "hold", par.get("hold"))
