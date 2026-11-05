# Sense-U Baby Pro (SU602) compatibility assessment

This is an assessment, not a claim of hardware support. Neither this component's
pairing/connection sequence nor its sensor and alarm outputs have been verified
on an SU602. ESPHome and Home Assistant must not be relied on as the sole means
of monitoring a child. Keep the device's own monitoring and alerts enabled.

The comparison below uses the reverse-engineered [sense-u-ble project](https://github.com/mao-mao-yu/sense-u-ble),
including `sense_u_ble/protocol.py`, `sense_u_ble/client.py`, and
`tools/pairing.py` (reviewed at commit `4e8dedb11ee907a5fadace540293dd2491c57422`). It is not a device
manufacturer's specification.

| Area | Current component versus Pro reference |
| --- | --- |
| GATT | The MAC-derived CHAR_1/2/3/4 prefixes match. The reference uses CHAR_1, CHAR_2 and CHAR_4 for its normal client, and its pairing tool also subscribes to CHAR_3. This component additionally requires a particular service and all four characteristics and descriptors; the reference does not establish that this discovery requirement works on every SU602. |
| Baby code, `0x69`, `0x68` | Both register the same UID and obtain six code bytes from a successful `0x68` response. This component stores the code in ESPHome preferences or accepts a configured code. Its `0x68` packet is 10 bytes and uses uptime in milliseconds; the reference pairing tool sends 18 bytes with Unix seconds. Compatibility remains unverified. |
| `0x70` | Both send the six-byte code, but this component sends 15 bytes and uses uptime in milliseconds; the reference sends 18 bytes and Unix seconds. SU602 authentication is unverified. |
| `0xC0` | Both issue a 20-byte `0xC0 01` GET_BATCH after successful authentication. |
| `0xF5`, `0xB2`, `0xB3`, `0xB0` | The original component writes `F5 F2` after `0xC0`, then on first configuration writes fixed temperature, activity and breath alarm values. The reference uses `F5 F0`, explicitly disabling a prone-alert bit, followed by fixed alarm settings. These writes affect monitoring: with `preserve_monitoring_settings: true`, this component skips its F5/B2/B3/B0 chain, including the F5 commands from its power switch. It still sends `0x68` during pairing and `0xC0` after authentication; their complete effects have not been independently verified. Whether the Pro works without the settings chain is unknown. Existing configurations retain their prior behavior unless they enable this option. |
| `0xBA` | The reference polls CHAR_4 with a 20-byte request and documents posture, temperature, humidity, breath and battery offsets in the response. This component neither sends nor decodes `0xBA`. Humidity is therefore omitted from the Pro example: the reference describes a field, but the measurement and availability have not been independently confirmed. |
| `0xF6` | The reference allows a consumer to request an alert ACK; this component does not send `0xF6`. No automatic ACK has been added. |
| Alerts | Existing record decoding recognizes posture mode 2 and breath modes 8/9. The reference describes modes 10 and 65 as combined prone/weak-breath events. This component does not decode those combined modes, and its alarm binary sensors are reset on disconnect. They cannot be treated as a complete or reliable SU602 alarm indication. The Pro example deliberately leaves them out. |

The example's BLE Connected entity indicates successful protocol authentication
in the existing implementation, not verified delivery of fresh readings. A
connected state alone does not prove monitoring or alarms are working. Initial
pairing may invalidate pairing with another client; see the project's pairing
instructions. The baby code is sensitive and the existing component logs it.
