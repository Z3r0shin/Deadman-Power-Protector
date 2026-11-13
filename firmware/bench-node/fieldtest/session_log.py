# Logs the bench node's state once a second and prints a line on every change of the fields a bench test watches
# (phase, warning, alarm and minor fault, relay, link, presence, radar, keys, buzzer). Usage:
#   python session_log.py <log file> <minutes>
import base64, json, os, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
C = json.load(open(os.path.join(HERE, "config.json")))  # gitignored
B = "http://" + C["bench_host"]
AUTH = "Basic " + base64.b64encode((C["user"] + ":" + C["password"]).encode()).decode()
LOG = open(sys.argv[1], "w", buffering=1)
END = time.time() + float(sys.argv[2]) * 60


def state():
    try:
        req = urllib.request.Request(B + "/api/state", headers={"Authorization": AUTH})
        return json.loads(urllib.request.urlopen(req, timeout=3).read())
    except Exception as e:
        return {"err": str(e)[:60]}


prev = None
while time.time() < END:
    s = state()
    LOG.write(json.dumps({"clock": time.strftime("%H:%M:%S"), "s": s}) + "\n")
    if "err" in s:
        key = ("no answer",)
    else:
        key = (s["phase"], s["warn"]["on"], s["warn"]["why"], s["alarm"]["code"], s["alarm"]["minor"],
               s["alarm"]["minor_silenced"], s["bar"]["shelly"], s["bar"]["link"], s["away"]["seen"], s["radar"]["ok"],
               tuple(s["keys"]), s["buz"])
    if key != prev:
        print(time.strftime("%H:%M:%S"), key if "err" in s else
              "phase=%s warn=%s/%s left=%s alarm=%s minor=%s silenced=%s relay=%s link=%s seen=%s radar=%s keys=%s buz=%s" % (
                  s["phase"], s["warn"]["on"], s["warn"]["why"], s["warn"]["left"], s["alarm"]["code"] or "-",
                  s["alarm"]["minor"], s["alarm"]["minor_silenced"], s["bar"]["shelly"], s["bar"]["link"],
                  s["away"]["seen"], s["radar"]["ok"], s["keys"], s["buz"]), flush=True)
        prev = key
    time.sleep(1)
