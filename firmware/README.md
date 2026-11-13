# Deadman Power Protector firmware

Bench node firmware `v.1.0.20261113T000224Z` (release version 1) and the Shelly's lease script. Start with the pin
file.

| Path | What |
|---|---|
| [`bench-node/firmware/src/pins.h`](bench-node/firmware/src/pins.h) | The pin file: every pin of the bench node. Open this first. |
| [`bench-node/firmware/`](bench-node/firmware/) | The bench node firmware, a PlatformIO project (environment `bench`), with its PC tests in `test/` |
| [`bench-node/firmware/include/secrets.example.h`](bench-node/firmware/include/secrets.example.h) | Copy to `secrets.h` and fill in before building |
| [`bench-node/hwtest/`](bench-node/hwtest/) | Hardware-test firmware, a separate PlatformIO project; not the firmware the device runs |
| [`bench-node/fieldtest/`](bench-node/fieldtest/) | Field tests run from a computer against a real bench node and Shelly. They switch the real bar. Copy `config.example.json` to `config.json` and put in your devices' addresses, the web login and both MACs |
| [`shelly-lease/lease.js`](shelly-lease/lease.js) | The Shelly's lease script, with its PC tests in `shelly-lease/test/` |
| `bench-node/bench-node-test.yaml`, `bench_ui.h`, `tests/`, `secrets.example.yaml` | An ESPHome draft, not used by the build |

How to build, flash and configure it is in the Firmware Gospel, in [`../docs/`](../docs/).
Back to the [main README](../README.md).
