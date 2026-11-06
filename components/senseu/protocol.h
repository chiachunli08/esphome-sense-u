#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

// Protocol reference: mao-mao-yu/sense-u-ble (MIT), see THIRD_PARTY_NOTICES.md.
namespace esphome {
namespace senseu {
namespace protocol {

struct Snapshot {
  uint8_t posture;
  float temperature;
  float humidity;
  float breath;
  float activity;
  float rssi;
  float battery;
  bool wearing;
  uint8_t charge;
};

inline bool decode_snapshot(const uint8_t *data, size_t size, Snapshot &out) {
  // Require the complete snapshot, including charge; never read partial packets.
  if (size < 12 || data[0] != 0xBA)
    return false;
  out.posture = data[2];
  const float temperature = (data[3] | (uint16_t(data[4]) << 8)) / 10.0f;
  out.temperature = temperature > 10.0f && temperature < 50.0f ? temperature : NAN;
  out.humidity = data[5] <= 100 ? float(data[5]) : NAN;
  out.breath = data[6] < 200 ? float(data[6]) : NAN;
  out.activity = data[7];
  out.rssi = data[8] < 128 ? float(data[8]) : float(int(data[8]) - 256);
  out.battery = data[9] <= 100 ? float(data[9]) : NAN;
  out.wearing = data[10] == 0x81;
  out.charge = data[11];
  return true;
}

inline size_t record_min_length(uint8_t record, uint8_t status) {
  if (record == 6) {
    if (status == 1) return 7;
    if (status == 2 || status == 3 || status == 4) return 2;
  }
  if (record == 8) {
    if (status == 1) return 8;
    if (status == 2) return 7;
    if (status == 4 || status == 5 || status == 7) return 6;
  }
  return 0;  // Unsupported record, not fresh sensor data.
}

inline const char *posture_name(uint8_t posture, bool pro) {
  switch (posture) {
    case 0: return "on back";
    case 1: return "on belly";
    case 2: return "on left";
    case 3: return "on right";
    case 4: return pro ? "sitting" : "Unknown";
    case 5: return pro ? "Unavailable (charging)" : "Unknown";
    default: return "Unknown";
  }
}

// Independent alert bits prevent clearing one event from erasing another.
inline uint8_t breath_alert_bit(uint8_t mode, bool pro) {
  switch (mode) {
    case 8: return 1;
    case 9: return 2;
    case 10: return pro ? 4 : 0;
    case 65: return pro ? 8 : 0;
    default: return 0;
  }
}
inline uint8_t posture_alert_bit(uint8_t mode, bool pro) {
  switch (mode) {
    case 2: return 1;
    case 10: return pro ? 2 : 0;
    case 65: return pro ? 4 : 0;
    default: return 0;
  }
}
inline uint8_t temperature_alert_bit(uint8_t mode) {
  return mode == 3 ? 1 : mode == 4 ? 2 : mode == 7 ? 4 : 0;
}
inline void update_alert_bits(uint8_t &bits, uint8_t bit, bool active) {
  if (active) bits |= bit;
  else bits &= ~bit;
}

}  // namespace protocol
}  // namespace senseu
}  // namespace esphome
