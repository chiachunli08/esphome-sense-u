# Sense-U Baby Pro (SU602): implementation and validation

This is an experimental integration, not a verified infant safety monitor.
Keep the device's own monitoring and alerts enabled. Compilation and synthetic
packet tests do not establish measurement accuracy or complete alarm coverage.

## Implemented

Enable `pro_mode: true` in the [C3 example](example-sense-u-pro-esp32c3.yaml).
Use ESPHome 2026.10.2 or later. Reference: the reverse-engineered
[mao-mao-yu/sense-u-ble](https://github.com/mao-mao-yu/sense-u-ble), not a
manufacturer specification.

- After successful `70 00` authentication and the `C0` reply, send a 20-byte
  `0xBA` request on CHAR_4 with GATT write response. Repeat every `update_interval`
  (default 2s, minimum 1s). A stalled initialization allows read-only polling
  after 20s; polling never begins before authentication.
- Decode a complete snapshot of at least 12 bytes: posture [2], temperature
  [3..4] little-endian / 10, experimental humidity [5] in percent, breath [6],
  activity [7], signed device RSSI [8], battery [9], wearing [10] == 0x81,
  and charge [11]. Invalid temperature/breath/battery/humidity values become NaN.
- Snapshot temperature accepts 10 < Celsius < 50, breath < 200 and percentages
  <= 100, matching the reference's principal validation ranges. These are
  parsing limits, NOT medical thresholds. Zero breath is a valid value.
- Posture 4 is sitting. Posture 5 means unavailable while charging, not a
  position. Charge is separate: Not charging / Charging / Full / Unknown.
- Decode combined alert modes 10 and 65 as breath + posture events. Track
  overlapping modes separately so clearing one does not clear another.
  No `0xF6` ACK is sent, automatically or otherwise. Unrecognized event modes
  are logged. Alarm state stays unknown until an applicable event is received.
- Clear readings, wearing and alarm availability on disconnect or data timeout.
  Pro `data_fresh` specifically requires a complete BA snapshot; isolated
  battery/alert pushes do not keep old breath/temperature values looking fresh.
- Wait for all required notification descriptor acknowledgements before auth.
  Pro requires CHAR_1/2/4; legacy mode retains CHAR_3. Rediscover handles after
  every disconnect. Report pairing rejection, authentication failure, GATT
  failures and timeout instead of staying indefinitely at Connecting.
- Keep the original flash preference key and packed data layout. Save a complete
  baby code before disconnecting after pairing. Never print pairing codes or raw
  auth bytes in logs. Existing credentials are not erased on authentication failure.

## Configuration and limits

| Option | Default | Meaning |
| --- | --- | --- |
| `pro_mode` | false | Enable Pro snapshots and extended event decoding. |
| `preserve_monitoring_settings` | true for Pro, false otherwise | Skip F5/B2/B3/B0 commands, including power-switch F5 commands. Does not restore prior settings. |
| `update_interval` | 2s | Pro polling interval, at least 1s. |
| `data_timeout` | 30s | Must exceed update_interval. Clear data and reconnect if snapshots stop (legacy: supported measurement records). |
| `connection_timeout` | 30s | Timeout from GATT open through authentication. |
| `baby_code` | absent | Reuse flash code, or supply a known 12-character hex string via secrets. |

The preserved-settings mode still sends `0x69`/`0x68` for first pairing, `0x70`
for authentication and `0xC0` after authentication. Their full device-side effects
have not been independently verified. First pairing can invalidate another
client's pairing. Unlike the reference, this implementation intentionally retains
the existing 10-byte `0x68` / 15-byte `0x70` and uptime timestamps because the
user reported successful pairing with that flow. The reference uses 18 bytes and
Unix seconds; compatibility with all Pro firmware versions is not established.

The reference initializes F5/B2/B3/B0 and says it is needed for CHAR_2 alerts.
This component does NOT assume alerts will arrive when preserving settings.
The old F5 F2 settings chain remains available only if explicitly opted into for
Pro; it writes hardcoded settings. Do not toggle it merely to make readings work.

Humidity is present in the reverse-engineered layout but its physical meaning
and availability need hardware verification. Device-reported RSSI may differ
from ESP32 receiver signal strength. Unknown alarm state must not be interpreted
as safe/no alarm. The current source is tested with ESPHome 2026.10.2; older
ESPHome versions are not supported by these tests.

## Flash and verify

1. Keep the existing node name, BLE MAC, and flash pairing. Merge the `senseu`
   options above into your current YAML or use the full example with local secrets.
2. Fetch the latest external component (`refresh: always`) and compile/install.
   Do not use erase-flash/factory-reset as part of this upgrade.
3. Look for `Connected successfully!`, TX `0xC0`, TX `0xBA`, RX `0xBA` and
   `Receiving data`. `connected` and `data_fresh` convey different stages.
4. Compare values with the device/app in separate sessions; only one BLE client
   can connect at a time. Test disconnected/stale behavior and charge/removed
   states off-body. New snapshot values and alarm coverage remain unverified
   until this hardware validation is complete.

## Local checks

```sh
pip install esphome==2026.10.2 pytest
pytest -q tests/test_schema.py
c++ -std=c++17 -fsanitize=address,undefined -I. tests/test_protocol.cpp -o /tmp/test-protocol
/tmp/test-protocol
c++ -std=c++17 -Itests/stubs -I. tests/test_session.cpp components/senseu/senseu.cpp -o /tmp/test-session
/tmp/test-session
esphome compile tests/compile-pro.yaml
```

Fixtures are synthetic and include truncations, invalid field values, zero breath,
signed RSSI, charging posture, overlapping alarm modes and randomized packet sizes.
The compile config contains dummy network values and is not intended to be flashed.

The session test executes the real component against a fake GATT transport and
clock: rejected pairing, saved-code reboot, descriptor ordering, authentication,
polling, settings preservation, overlapping alerts, data expiry and timer rollover.
It does not simulate ESP-IDF scheduling or radio behavior.
