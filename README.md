# ESPHome Sense-U Baby Monitor

This project aims to provide an ESPHome integration for the Sense-U baby monitor by directly communicating via BLE with the Sense-U button.

There is now also support for replacing the firmware of the Base Station 2 with ESPHome, since the Base Station 2 is powered by an ESP32 module. See below for details.

If you like my work, consider sponsoring this project via [Github Sponsors](https://github.com/sponsors/andyboeh) or by acquiring one of my [Amazon Wishlist](https://www.amazon.de/hz/wishlist/ls/ROO2X0G63PCT?ref_=wl_share) items.

## Status

The basics are working for this component. However, some functionality might be missing or might be unimplemented. 
The Base Station 2 is, apart from the temperature sensor, fully supported.

## Baby Pro (SU602)

Use [example-sense-u-pro-esp32c3.yaml](example-sense-u-pro-esp32c3.yaml) with
ESPHome **2026.10.2 or later**. Set `pro_mode: true` to enable read-only `0xBA`
snapshot polling (default every 2s). Legacy configs leave Pro polling disabled.
The Pro mode defaults to `preserve_monitoring_settings: true` and does not send
F5/B2/B3/B0 settings or F6 alert acknowledgements. It still uses the original
pairing/authentication flow and GET_BATCH command.

New optional entities: `sensor.activity`, `sensor.signal_strength` (device-reported
RSSI), `binary_sensor.wearing`, `binary_sensor.data_fresh`, and
`text_sensor.charge_status`. Existing breath, temperature, battery and posture
entities are filled by snapshots. Humidity is an experimental protocol field.
`connected` means authenticated; `data_fresh` in Pro mode means a complete snapshot
arrived within `data_timeout` (default 30s). A timeout clears measurements and
reconnects. Alarm entities start unknown and become unknown on disconnect, not OFF.

```yaml
senseu:
  - id: senseu_device
    ble_client_id: senseu_ble
    pro_mode: true
    preserve_monitoring_settings: true
    update_interval: 2s
    data_timeout: 30s
    connection_timeout: 30s
```

Use the same BLE MAC and do not erase flash: the existing pairing key/layout is
preserved across updates. `baby_code` is optional and must be a quoted 12-digit
hex string if supplied. The logger no longer prints it or raw auth packets.
See [compatibility and verification](SU602-COMPATIBILITY.md) for limitations;
new snapshot and alert behavior still needs hardware validation.

## Getting started

These steps assume that your Sense-U is **not** paired to an app/the base station. If it is paired, then the pairing is lost. Technically, it is possible to connect the sensor to app/base station and ESPHome, but not at the same time. See section "Sharing baby code".

  1. Add the required configuration to the YAML file - see [example_sense-u.yaml](example_sense-u.yaml) for a template. Do not provide a baby code.
  2. Compile and upload, start log output. You will see a message similar to "Could not register UID, enable pairing mode and try again."
  3. Enable pairing mode on the Sense-U button by double-shaking the sensor. It starts blinking.
  4. Observe the log, the next connection attempt should be successful.

Once you've got the basics set up, you can use on of the other provided example files to add features like local alarm.

## Sharing baby code

The pairing procedure is not a pairing at the BLE level, but rather at the application level. The Sense-U generates a Baby Code that needs to be provided during initialization. 
If you capture the relevant BLE packet on Android during pairing, you can reuse the baby code for ESPHome. You can then connect using your phone and using ESPHome, but not simultaneously.

For multiple ESPHome devices, store a known code locally in `secrets.yaml`:

```yaml
senseu_baby_code: "012345abcdef"  # illustrative only; use your device's real code
```

Then set `baby_code: !secret senseu_baby_code` under `senseu`. This version no
longer logs pairing credentials. Existing flash-stored codes are still used;
if no code is known, obtaining one requires a controlled pairing capture rather
than reading the normal DEBUG log. Never publish captured credentials.

## Protocol

See a description on the BLE protocol in [protocol.md](protocol.md)

## Base Station 2

The Base Station 2 can be converted to ESPHome. The device needs to be disassembled, inside you will find three pins labelled "IO0", "ESP32_RX" and "ESP32_TX". On another unpopulated header you will find 5V and GND pins. Use these pins for flashing, **do not connect USB**.

**Attention!** Do not connect the base station via USB to a computer. Instead of USB data signals, the ESP32 Rx, Tx and IO0 signals are present on the Micro USB connector. You could use a specially prepared cable to flash the base station.

If you want to make a backup first, you can do so by running `esptool.py`:
```
esptool.py -b 115200 --port /dev/ttyUSB0 read_flash 0x00000 0x400000 flash_4M.bin
```

An exemplary configuration file will follow soon, for the time being, this information should be sufficient:

```
output:
  - platform: ledc
    pin: GPIO4
    id: buzzer

  - platform: ledc
    pin: GPIO17
    id: status_output_green

  - platform: ledc
    pin: GPIO18
    id: status_output_blue

  - platform: ledc
    pin: GPIO5
    id: status_output_red

  - platform: ledc
    pin: GPIO27
    id: main_output_blue

  - platform: ledc
    pin: GPIO32
    id: main_output_green

  - platform: ledc
    pin: GPIO33
    id: main_output_red

switch:
  - platform: template
    turn_on_action:
      - output.turn_on: buzzer
      - output.ledc.set_frequency:
          id: buzzer
          frequency: "1000Hz"
      - output.set_level:
          id: buzzer
          level: "50%"
    turn_off_action:
      - output.turn_off: buzzer
    name: Piezo On

binary_sensor:
  - platform: gpio
    pin:
      number: GPIO25
      inverted: yes
    name: "Reset Button"
    id: reset_button

  - platform: gpio
    pin: GPIO14
    id: touch_sensor
    name: "Touch Sensor"

light:
  - platform: rgb
    name: "Status Light"
    red: status_output_red
    green: status_output_green
    blue: status_output_blue

  - platform: rgb
    name: "Main Light"
    red: main_output_red
    green: main_output_green
    blue: main_output_blue
```

## TODO

  * Provide more examples how to handle alerts
  * Improve battery sensor
  * Investigate base station