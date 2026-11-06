#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/preferences.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>

namespace esphome {
namespace senseu {

namespace espbt = esphome::esp32_ble_tracker;

static const std::string BASE_SERVICE_UUID_PREFIX = "0102ca20-9e06-a079-2e3f-";
static const std::string DATA_CHAR_1_PREFIX = "01021921-9e06-a079-2e3f-";
static const std::string DATA_CHAR_2_PREFIX = "01021922-9e06-a079-2e3f-";
static const std::string DATA_CHAR_3_PREFIX = "01021923-9e06-a079-2e3f-";
static const std::string DATA_CHAR_4_PREFIX = "01021925-9e06-a079-2e3f-";
static uint8_t SET_UID_DATA[] = {0x69, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x00 };

enum WRITE_REQ {
  UID_DATA,
  REGISTER_TYPE,
  RECONNECTION_TYPE,
  GET_BATCH,
  LEANING_TYPE,
  TEMP_ALARM,
  KICKING_ALARM,
  BREATH_ALARM,
  POWER_ON,
  POWER_OFF,
  GET_BABY_DATA,
};

struct SenseUStorage {
  bool paired;
  bool configured;
  uint8_t baby_code[6];
} PACKED;

class SenseU : public esphome::ble_client::BLEClientNode, public Component {
 public:
  void setup() override;
  void loop() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_breath_rate(sensor::Sensor *breath_rate) { breath_rate_ = breath_rate; }
  void set_temperature(sensor::Sensor *temperature) { temperature_ = temperature; }
  void set_humidity(sensor::Sensor *humidity) { humidity_ = humidity; }
  void set_posture_sensor(text_sensor::TextSensor *posture) { posture_ = posture; }
  void set_status_sensor(text_sensor::TextSensor *status) { status_ = status; }
  void set_battery_level(sensor::Sensor *battery_level) { battery_level_ = battery_level; }
  void set_breath_alarm(binary_sensor::BinarySensor *breath_alarm ) { breath_alarm_ = breath_alarm; }
  void set_posture_alarm(binary_sensor::BinarySensor *posture_alarm ) { posture_alarm_ = posture_alarm; }
  void set_temperature_alarm(binary_sensor::BinarySensor *temperature_alarm) { temperature_alarm_ = temperature_alarm; }
  void set_battery_alarm(binary_sensor::BinarySensor *battery_alarm) { battery_alarm_ = battery_alarm; }
  void set_connected_sensor(binary_sensor::BinarySensor *connected_sensor) { connected_sensor_ = connected_sensor; }
  void set_power_switch(bool state);
  void set_preserve_monitoring_settings(bool preserve) { preserve_monitoring_settings_ = preserve; }
  void set_pro_mode(bool pro) { pro_mode_ = pro; }
  void set_update_interval(uint32_t interval) { update_interval_ = interval; }
  void set_data_timeout(uint32_t timeout) { data_timeout_ = timeout; }
  void set_connection_timeout(uint32_t timeout) { connection_timeout_ = timeout; }
  void set_activity(sensor::Sensor *s) { activity_ = s; }
  void set_signal_strength(sensor::Sensor *s) { signal_strength_ = s; }
  void set_wearing(binary_sensor::BinarySensor *s) { wearing_ = s; }
  void set_data_fresh(binary_sensor::BinarySensor *s) { data_fresh_ = s; }
  void set_charge_status(text_sensor::TextSensor *s) { charge_status_ = s; }
  void set_baby_code(std::string baby_code) { baby_code_ = baby_code; }

 private:
  bool write_char(WRITE_REQ cmd);
  void reset_connection_();
  void invalidate_data_();
  void mark_data_received_(bool snapshot = false);
  void fail_connection_(const char *reason);
  void handle_notification_(uint16_t handle, const uint8_t *data, size_t size);
  void publish_posture_(uint8_t posture);
  void publish_charge_(uint8_t charge);
  bool discover_characteristics();

  void enable_notifications();
  void store_preferences();

  ESPPreferenceObject pref_;
  SenseUStorage pref_storage_{};
  espbt::ESPBTUUID base_service_uuid_;
  espbt::ESPBTUUID data_char_1_uuid_;
  espbt::ESPBTUUID data_char_2_uuid_;
  espbt::ESPBTUUID data_char_3_uuid_;
  espbt::ESPBTUUID data_char_4_uuid_;
  std::string baby_code_;
  uint16_t char_handle_1_{0};
  uint16_t cccd_1_{0};
  uint16_t char_handle_2_{0};
  uint16_t cccd_2_{0};
  uint16_t char_handle_3_{0};
  uint16_t cccd_3_{0};
  uint16_t char_handle_4_{0};
  uint16_t cccd_4_{0};
  bool pro_mode_{false};
  bool session_active_{false};
  bool authenticated_{false};
  bool fresh_{false};
  bool auth_sent_{false};
  bool failed_{false};
  bool poll_ready_{false};
  uint8_t notify_ready_{0};
  uint8_t breath_alert_bits_{0};
  uint8_t posture_alert_bits_{0};
  uint8_t temperature_alert_bits_{0};
  uint32_t update_interval_{2000};
  uint32_t data_timeout_{30000};
  uint32_t connection_timeout_{30000};
  uint32_t connection_started_{0};
  uint32_t authenticated_at_{0};
  uint32_t last_poll_{0};
  uint32_t last_data_{0};
  sensor::Sensor *activity_{nullptr};
  sensor::Sensor *signal_strength_{nullptr};
  binary_sensor::BinarySensor *wearing_{nullptr};
  binary_sensor::BinarySensor *data_fresh_{nullptr};
  text_sensor::TextSensor *charge_status_{nullptr};
  bool power_state_{true};
  bool preserve_monitoring_settings_{false};
  sensor::Sensor *breath_rate_{nullptr};
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};
  sensor::Sensor *battery_level_{nullptr};
  text_sensor::TextSensor *posture_{nullptr};
  text_sensor::TextSensor *status_{nullptr};
  binary_sensor::BinarySensor *breath_alarm_{nullptr};
  binary_sensor::BinarySensor *posture_alarm_{nullptr};
  binary_sensor::BinarySensor *temperature_alarm_{nullptr};
  binary_sensor::BinarySensor *battery_alarm_{nullptr};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
};

}  // namespace senseu
}  // namespace esphome

#endif

