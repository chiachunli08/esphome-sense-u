#include "components/senseu/protocol.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <random>
using namespace esphome::senseu::protocol;
int main() {
  // Synthetic fixture, NOT a capture from the user's device.
  const uint8_t bytes[] = {0xBA, 0, 1, 0x6D, 1, 55, 32, 12, 0xC2, 78, 0x81, 2};
  Snapshot s{};
  assert(decode_snapshot(bytes, sizeof(bytes), s));
  assert(s.posture == 1 && s.temperature == 36.5f && s.humidity == 55);
  assert(s.breath == 32 && s.activity == 12 && s.rssi == -62 && s.battery == 78);
  assert(s.wearing && s.charge == 2);
  // Every truncation must be rejected before reading any missing field.
  for (size_t n = 0; n < sizeof(bytes); n++) {
    std::vector<uint8_t> packet(bytes, bytes + n);
    assert(!decode_snapshot(packet.data(), n, s));
  }
  uint8_t invalid[12]; memcpy(invalid, bytes, 12);
  invalid[0] = 0x70;
  assert(!decode_snapshot(invalid, 12, s));
  invalid[0] = 0xBA;
  invalid[3] = 0; invalid[4] = 0; invalid[5] = 255;
  invalid[6] = 200; invalid[9] = 101; invalid[10] = 0x80;
  assert(decode_snapshot(invalid, 12, s));
  assert(std::isnan(s.temperature) && std::isnan(s.humidity));
  assert(std::isnan(s.breath) && std::isnan(s.battery) && !s.wearing);
  // Zero breath and zero battery are valid, not missing values.
  invalid[6] = invalid[9] = 0;
  assert(decode_snapshot(invalid, 12, s) && s.breath == 0 && s.battery == 0);
  assert(!strcmp(posture_name(5, true), "Unavailable (charging)"));
  assert(!strcmp(posture_name(4, true), "sitting"));
  assert(!strcmp(posture_name(4, false), "Unknown"));
  uint8_t alerts = 0;
  update_alert_bits(alerts, breath_alert_bit(9, true), true);
  update_alert_bits(alerts, breath_alert_bit(10, true), true);
  update_alert_bits(alerts, breath_alert_bit(9, true), false);
  assert(alerts != 0); // Clearing mode 9 must not clear active combined mode 10.
  update_alert_bits(alerts, breath_alert_bit(10, true), false);
  assert(alerts == 0);
  assert(breath_alert_bit(65, true) && posture_alert_bit(65, true));
  assert(!breath_alert_bit(65, false));
  assert(record_min_length(8, 1) == 8 && record_min_length(8, 2) == 7);
  assert(record_min_length(8, 4) == 6 && record_min_length(6, 1) == 7);
  assert(record_min_length(23, 8) == 0); // BA must not fall through as a record.
  std::mt19937 random(602);
  for (unsigned n = 0; n < 10000; n++) {
    std::vector<uint8_t> packet(random() % 40);
    for (auto &value : packet) value = random();
    decode_snapshot(packet.data(), packet.size(), s);
  }
}
