# Test: the bench node unplugged with the bar on; the Shelly must cut by its own lease timer.
# Turns the bar on, then waits (up to 20 min) for you to unplug the bench node, and logs the Shelly every
# second until the relay is off and the bench node answers again. Usage: python unplug_test.py <log file>.
import base64, json, os, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
C = json.load(open(os.path.join(HERE, "config.json")))  # gitignored
B = "http://" + C["bench_host"]
S = "http://" + C["shelly_host"]
AUTH = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
LOG = open(sys.argv[1], "w", buffering=1)


def say(msg):
    line = "%s %s" % (time.strftime("%H:%M:%S"), msg)
    print(line, flush=True)
    LOG.write(line + "\n")


def bench():
    try:
        req = urllib.request.Request(B + "/api/state", headers={"Authorization": AUTH})
        return json.loads(urllib.request.urlopen(req, timeout=2).read())
    except Exception:
        return None


def relay():
    try:
        return json.loads(urllib.request.urlopen(S + "/rpc/Switch.GetStatus?id=0", timeout=3).read())
    except Exception:
        return None


s = bench()
if not s or s["mac"] != C["bench_mac"] or s["warn"]["on"] or s["alarm"]["on"]:
    raise SystemExit("bench node not ready")
req = urllib.request.Request(B + "/api/act", data=urllib.parse.urlencode({"a": "bar.on"}).encode(),
                             headers={"Authorization": AUTH})
say("bar.on -> %s" % urllib.request.urlopen(req, timeout=8).read().decode())
time.sleep(3)
sw = relay()
say("relay %s, timer %s s: unplug the bench node now" % (sw and sw.get("output"), sw and sw.get("timer_duration")))

gone_at = None
end = time.time() + 1200
while time.time() < end:
    if bench() is None:
        gone_at = time.time()
        sw = relay()
        say("bench node not answering: relay %s, timer %s s, started at %s" % (
            sw and sw.get("output"), sw and sw.get("timer_duration"), sw and sw.get("timer_started_at")))
        break
    time.sleep(1)
if gone_at is None:
    raise SystemExit("the bench node was not unplugged within 20 min; the bar stays as the bench node decides")

while True:
    sw = relay()
    LOG.write(json.dumps({"clock": time.strftime("%H:%M:%S"), "relay": sw}) + "\n")
    if sw and sw.get("output") is False:
        say("relay off, source %s, %.0f s after the bench node stopped answering" % (sw.get("source"), time.time() - gone_at))
        break
    if time.time() - gone_at > 400:
        say("relay still on 400 s after the bench node stopped answering")
        break
    time.sleep(1)

end = time.time() + 1800
while time.time() < end:
    s = bench()
    if s:
        say("bench node back: %s %s up %s s, phase %s, alarm %s" % (s["fw"], s["commit"], s["up"], s["phase"], s["alarm"]["code"]))
        break
    time.sleep(2)
