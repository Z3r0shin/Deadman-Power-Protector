// test/run.js — test harness for lease.js. Run it with Node, from the shelly-lease folder:
//   node test/run.js
// Node built-ins only (vm, fs, path, assert) — nothing installed.

"use strict";

const assert = require("assert");
const path = require("path");
const { createMock } = require("./shelly_mock.js");

const LEASE_PATH = path.join(__dirname, "..", "lease.js");

const results = [];

function test(name, fn) {
  try {
    fn();
    results.push({ name: name, pass: true });
  } catch (e) {
    results.push({ name: name, pass: false, error: e && e.message ? e.message : String(e) });
  }
}

function newMock(opts) {
  const m = createMock(opts);
  m.load(LEASE_PATH);
  return m;
}

// 1. renew while off is refused and stays off.
test("renew while off refused, stays off", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=renew&l=120");
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "off");
  assert.strictEqual(r.json.on, false);
  assert.strictEqual(r.json.lease_s, 0);
  assert.strictEqual(m.state.switch.output, false);
});

// 2. on press=1 sets ceiling and toggle_after.
test("on press=1 sets ceiling and toggle_after", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=on&press=1&l=300&c=3600");
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.on, true);
  assert.strictEqual(r.json.lease_s, 300);
  assert.strictEqual(r.json.ceil_s, 3600);
  assert.strictEqual(m.state.switch.output, true);
  assert.strictEqual(m.state.switch.timer_duration, 300);
});

// Helper: keep a lease alive with periodic renews (stepMs must stay under the relay's own
// toggle_after so the fake relay-side flip-back timer never fires on its own) up to a given
// amount of device uptime, landing exactly on uptoMs.
function keepRenewedUpTo(m, uptoMs, stepMs, l) {
  let t = 0;
  while (t + stepMs < uptoMs) {
    m.tick(stepMs);
    t = t + stepMs;
    const rr = m.callEndpoint("lease", "op=renew&l=" + l);
    assert.strictEqual(rr.json.ok, true);
  }
  const rem = uptoMs - t;
  if (rem > 0) {
    m.tick(rem);
  }
}

// 3. renew never extends past the ceiling (toggle_after shrinks near it).
test("renew never extends past the ceiling", function () {
  const m = newMock({ initialOn: false });
  m.callEndpoint("lease", "op=on&press=1&l=300&c=3600");
  keepRenewedUpTo(m, 3580 * 1000, 250 * 1000, 300); // land at t=3580s, 20 s left to the ceiling
  const r = m.callEndpoint("lease", "op=renew&l=300");
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.lease_s, 20); // shrunk from 300 to the 20 s left
  assert.strictEqual(r.json.ceil_s, 20);
  assert.strictEqual(m.state.switch.timer_duration, 20);
});

// 4. ceiling reached -> off, and a subsequent renew is refused with why "ceiling". This
// exercises doRenew's own ceiling check directly: the relay is forced to still report "on"
// at the check (the real relay-side flip-back would, in this case, agree and have already
// turned it off too, since toggle_after was clamped to the ceiling at op=on time — but the
// script's own check must not depend on that coincidence, e.g. the periodic-reapply path
// can re-arm toggle_after without regard to an already-passed ceiling).
test("ceiling reached: off, renew refused why=ceiling", function () {
  const m = newMock({ initialOn: false });
  // c=62 (>= the 60s minimum, and not a multiple of the 5s periodic-check interval) so the
  // moment the ceiling passes doesn't coincide with a periodic tick, which would otherwise
  // catch it first and mask whether doRenew's own check works.
  m.callEndpoint("lease", "op=on&press=1&l=300&c=62"); // ceil_end = 62s from now
  m.tick(63 * 1000); // 1 s past the ceiling, still short of the next periodic tick (65s)
  m.state.switch.output = true; // force "still on" to isolate the ceiling check itself
  const r = m.callEndpoint("lease", "op=renew&l=300");
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "ceiling");
  assert.strictEqual(r.json.on, false);
  assert.strictEqual(m.state.switch.output, false);
});

// 5. press=1 resets the ceiling (even while already on).
test("press=1 resets ceiling", function () {
  const m = newMock({ initialOn: false });
  m.callEndpoint("lease", "op=on&press=1&l=300&c=600");
  m.tick(500 * 1000); // 100 s left to the old ceiling
  const r = m.callEndpoint("lease", "op=on&press=1&l=300&c=600");
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.ceil_s, 600); // fresh ceiling, not ~100
});

// 6. press=0 while on does not reset the ceiling.
test("press=0 while on does not reset ceiling", function () {
  const m = newMock({ initialOn: false });
  // l=550 so the relay's own flip-back timer (550s) outlasts the 500s tick below, i.e. the
  // relay is still genuinely on (not just script-believed-on) when press=0 arrives.
  m.callEndpoint("lease", "op=on&press=1&l=550&c=600");
  m.tick(500 * 1000); // 100 s left to the ceiling
  const r = m.callEndpoint("lease", "op=on&press=0&l=300&c=600");
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.ceil_s, 100); // unchanged ceiling, just a renew
});

// 7. press=0 while off starts a fresh period (same as a press).
test("press=0 while off starts fresh", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=on&press=0&l=300&c=600");
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.on, true);
  assert.strictEqual(r.json.ceil_s, 600);
  assert.strictEqual(m.state.switch.output, true);
});

// 8. bounds clamp l and c (c=99999 -> 14400; also l below/above bounds).
test("bounds clamp l and c", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=on&press=1&l=5&c=99999");
  assert.strictEqual(r.json.ceil_s, 14400); // c clamped to max
  assert.strictEqual(r.json.lease_s, 30); // l=5 clamped up to min 30

  const m2 = newMock({ initialOn: false });
  const r2 = m2.callEndpoint("lease", "op=on&press=1&l=99999&c=1");
  // c=1 clamps UP to C_MIN=60, and l=99999 clamps down to L_MAX=600, then shrinks to
  // min(l, ceilLeft=60) = 60.
  assert.strictEqual(r2.json.ceil_s, 60);
  assert.strictEqual(r2.json.lease_s, 60);
});

// 9. app turns the relay on without a timer -> script applies toggle_after within 5 s.
test("app turns relay on without timer: toggle_after applied within 5s", function () {
  const m = newMock({ initialOn: false });
  // Simulate the Shelly app/web UI/HA turning the relay on directly, bypassing the script
  // (no toggle_after timer set).
  m.state.switch.output = true;
  assert.strictEqual(typeof m.state.switch.timer_duration, "undefined");
  m.tick(5000); // one periodic check interval
  assert.strictEqual(m.state.switch.timer_duration, 300);
  assert.strictEqual(m.state.switch.output, true);
  const r = m.callEndpoint("lease", "op=status");
  assert.strictEqual(r.json.ceil_s, 14400); // a fresh ceiling was started too
});

// 10. script restart while on: a short ceiling (600 s), never a fresh 4 h.
test("script restart while on: short 600 s ceiling, not a fresh 4 h", function () {
  const m1 = newMock({ initialOn: false });
  m1.callEndpoint("lease", "op=on&press=1&l=300&c=14400");
  m1.tick(100 * 1000);
  const m2 = createMock({ initialOn: m1.state.switch.output, nowMs: m1.state.nowMs });
  m2.load(LEASE_PATH);
  const r = m2.callEndpoint("lease", "op=status");
  assert.strictEqual(r.json.on, true);
  assert.strictEqual(r.json.ceil_s, 600);
  // renew cannot extend past it; a key press (press=1) restores a full ceiling.
  const rn = m2.callEndpoint("lease", "op=renew&l=600");
  assert.ok(rn.json.lease_s <= 600);
  const p = m2.callEndpoint("lease", "op=on&press=1&l=300&c=14400");
  assert.strictEqual(p.json.ceil_s, 14400);
});

// 10b. Shelly reboot (uptime near 0) with the relay on: same short ceiling.
test("reboot with relay on: short 600 s ceiling", function () {
  const m = createMock({ initialOn: true, nowMs: 10 * 1000 });
  m.load(LEASE_PATH);
  const r = m.callEndpoint("lease", "op=status");
  assert.strictEqual(r.json.ceil_s, 600);
});

// 10c. Query parsing with junk: non-digit values fall back to defaults, never to 0 or huge.
test("junk query values fall back to defaults", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=on&press=1&l=1e9&c=-5");
  assert.strictEqual(r.json.lease_s, 300);
  assert.strictEqual(r.json.ceil_s, 14400);
  const r2 = m.callEndpoint("lease", "op=renew&l=99999999999");
  assert.strictEqual(r2.json.lease_s, 300);
  const r3 = m.callEndpoint("lease", "l=30&op=renew&junk");
  assert.strictEqual(r3.json.ok, true);
  assert.strictEqual(r3.json.lease_s, 30);
});

// 11. a bad op returns ok:false, why:"bad_op".
test("bad op -> ok:false why:bad_op", function () {
  const m = newMock({ initialOn: true });
  const r = m.callEndpoint("lease", "op=frobnicate");
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "bad_op");
});

// 12. if renewals stop, the relay goes off within l seconds (toggle_after).
test("if renewals stop, relay goes off within l seconds", function () {
  const m = newMock({ initialOn: false });
  const r = m.callEndpoint("lease", "op=on&press=1&l=120&c=3600");
  assert.strictEqual(r.json.lease_s, 120);
  m.tick(119 * 1000);
  assert.strictEqual(m.state.switch.output, true); // not yet
  m.tick(2 * 1000); // crosses the 120 s mark
  assert.strictEqual(m.state.switch.output, false);
});


// 13. On the device a Switch.Set lands a moment after Shelly.call returns. A renew
// that arrives after the 5 s check has sent the ceiling's off, but before the relay has turned off, is refused
// and does not switch the relay back on with a fresh short ceiling.
test("renew in the moment the ceiling cuts is refused, relay stays off", function () {
  const m = newMock({ initialOn: false });
  m.tick(500); // uptime counts whole seconds: the relay's own timer then ends 0.5 s after the ceiling
  m.callEndpoint("lease", "op=on&press=1&l=300&c=60");
  m.state.deferSwitch = true;
  m.tick(59500); // the 5 s check at 60 s finds the ceiling passed and sends the off
  assert.strictEqual(m.state.pendingSwitch.length, 1);
  assert.strictEqual(m.state.switch.output, true); // not applied yet
  const r = m.callEndpointPending("lease", "op=renew&l=300");
  m.flushSwitch();
  assert.strictEqual(r.sent, true);
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "off");
  assert.strictEqual(m.state.switch.output, false);
  m.state.deferSwitch = false;
  m.tick(10 * 1000);
  assert.strictEqual(m.state.switch.output, false);
});

// 14. The same moment for a remote on (press=0): it is a fresh period from off, as the contract says, never the
// short restart ceiling on a relay believed on.
test("remote on in the moment the ceiling cuts starts a fresh period", function () {
  const m = newMock({ initialOn: false });
  m.tick(500); // uptime counts whole seconds: the relay's own timer then ends 0.5 s after the ceiling
  m.callEndpoint("lease", "op=on&press=1&l=300&c=60");
  m.state.deferSwitch = true;
  m.tick(59500);
  const r = m.callEndpointPending("lease", "op=on&press=0&l=300&c=3600");
  m.flushSwitch();
  assert.strictEqual(r.sent, true);
  assert.strictEqual(r.json.ok, true);
  assert.strictEqual(r.json.on, true);
  assert.strictEqual(r.json.ceil_s, 3600);
  assert.strictEqual(m.state.switch.output, true);
});

// 15. A key press whose relay-on has not landed yet, when the 5 s check runs, keeps its fresh ceiling.
test("5 s check during a pending on keeps the fresh ceiling", function () {
  const m = newMock({ initialOn: false });
  m.state.deferSwitch = true;
  const r = m.callEndpointPending("lease", "op=on&press=1&l=300&c=14400");
  m.tick(5 * 1000); // the check runs while the relay still reads off
  m.flushSwitch();
  assert.strictEqual(r.sent, true);
  m.state.deferSwitch = false;
  const r2 = m.callEndpoint("lease", "op=renew&l=300");
  assert.strictEqual(r2.json.ok, true);
  assert.ok(r2.json.ceil_s > 14000, "ceil_s " + r2.json.ceil_s);
});

// 16. A Switch.Set whose answer never comes does not stop the 5 s check for good: after about 10 s it runs again.
test("a lost Switch.Set answer holds the 5 s check for about 10 s at most", function () {
  const m = newMock({ initialOn: true }); // on with no timer: the 5 s check reapplies one
  m.state.deferSwitch = true;
  m.tick(5 * 1000);
  assert.strictEqual(m.state.pendingSwitch.length, 1); // the reapply, about to be lost
  m.state.pendingSwitch = [];
  m.state.deferSwitch = false;
  m.tick(5 * 1000); // 10 s: still waiting
  assert.strictEqual(typeof m.state.switch.timer_duration, "undefined");
  m.tick(5 * 1000); // 15 s: given up on, and the check runs again
  assert.ok(m.state.log.some(function (l) { return l.indexOf("no answer") >= 0; }), m.state.log.join(" | "));
  assert.strictEqual(m.state.switch.timer_duration, 300);
});

// 17. An answer that comes after its call was given up on does not count, so it cannot hide a
// newer off that is still on its way.
test("a late answer after the give-up does not hide a newer off", function () {
  const m = newMock({ initialOn: true });
  m.state.deferSwitch = true;
  m.tick(5 * 1000);
  const lost = m.state.pendingSwitch.shift(); // the reapply's answer comes very late
  m.tick(10 * 1000); // given up on at 15 s; the check sends a new reapply
  m.flushSwitch(); // it is answered, so the late answer below is the only one left over
  const off = m.callEndpointPending("lease", "op=off");
  lost.cb({ was_on: true }, 0, "", lost.userdata); // the late answer
  const r = m.callEndpointPending("lease", "op=renew&l=300");
  m.flushSwitch();
  assert.strictEqual(off.sent, true);
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "off");
  assert.strictEqual(m.state.switch.output, false);
});

// 18. A key press while a renew's ceiling-off is on its way keeps its fresh ceiling.
test("key press during a renew's ceiling-off keeps the fresh ceiling", function () {
  const m = newMock({ initialOn: false });
  m.tick(500);
  m.callEndpoint("lease", "op=on&press=1&l=300&c=61"); // ceiling at uptime 61, between two 5 s checks
  m.tick(60400); // 60.9 s: uptime 60, the ceiling not passed yet
  m.state.deferSwitch = true;
  m.tick(200); // 61.1 s: uptime 61, the relay's own timer not ended yet (61.5 s)
  const r1 = m.callEndpointPending("lease", "op=renew&l=300"); // finds the ceiling passed: off sent
  assert.strictEqual(m.state.pendingSwitch.length, 1);
  const r2 = m.callEndpointPending("lease", "op=on&press=1&l=300&c=14400");
  m.flushSwitch();
  assert.strictEqual(r1.json.why, "ceiling");
  assert.strictEqual(r2.json.ok, true);
  m.state.deferSwitch = false;
  const r3 = m.callEndpoint("lease", "op=renew&l=300");
  assert.strictEqual(r3.json.ok, true);
  assert.ok(r3.json.ceil_s > 14000, "ceil_s " + r3.json.ceil_s);
});

// 19. A failed off is tried again by the 5 s check.
test("a failed ceiling off is tried again", function () {
  const m = newMock({ initialOn: false });
  m.tick(500);
  m.callEndpoint("lease", "op=on&press=1&l=300&c=60");
  m.state.deferSwitch = true;
  m.tick(59500); // the check sends the ceiling's off
  const c = m.state.pendingSwitch.shift();
  c.cb(null, -1, "test failure", c.userdata); // it fails; the relay stays on
  m.state.deferSwitch = false;
  const r = m.callEndpoint("lease", "op=renew&l=300"); // the ceiling still counts as passed: no 600 s restart ceiling
  assert.strictEqual(r.json.ok, false);
  assert.strictEqual(r.json.why, "ceiling");
  assert.strictEqual(m.state.switch.output, false);
});


// 20. An off whose answer never comes is tried again after the give-up, never followed by the
// short restart ceiling.
test("a lost ceiling off is tried again after the give-up", function () {
  const m = newMock({ initialOn: false });
  m.tick(500);
  m.callEndpoint("lease", "op=on&press=1&l=300&c=120");
  delete m.state.switch.timer_duration; // as if switched on from the Shelly's app: no timer
  m.tick(4500); // 5 s: the check reapplies toggle_after 300, past the ceiling
  m.state.deferSwitch = true;
  m.tick(115 * 1000); // 120 s: the check sends the ceiling's off; it is lost
  assert.strictEqual(m.state.pendingSwitch.length, 1);
  m.state.pendingSwitch = [];
  m.state.deferSwitch = false;
  m.tick(10 * 1000); // 130 s: given up on, and the off is sent again
  assert.strictEqual(m.state.switch.output, false);
  const r = m.callEndpoint("lease", "op=renew&l=300");
  assert.strictEqual(r.json.ok, false);
});

// --- report -----------------------------------------------------------------

let passCount = 0;
let failCount = 0;
results.forEach(function (r) {
  if (r.pass) {
    passCount = passCount + 1;
    console.log("PASS - " + r.name);
  } else {
    failCount = failCount + 1;
    console.log("FAIL - " + r.name + " :: " + r.error);
  }
});
console.log("");
console.log(passCount + " passed, " + failCount + " failed, " + results.length + " total");

if (failCount > 0) {
  process.exit(1);
}
