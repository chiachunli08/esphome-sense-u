#include "senseu.h"
#include "protocol.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstring>

#ifdef USE_ESP32
namespace esphome {
namespace senseu {
static const char *const TAG = "senseu";

void SenseU::dump_config() {
  ESP_LOGCONFIG(TAG, "SenseU: Pro mode=%s, preserve monitoring settings=%s", YESNO(pro_mode_),
                YESNO(preserve_monitoring_settings_));
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms; data timeout: %u ms", (unsigned) update_interval_,
                (unsigned) data_timeout_);
  LOG_SENSOR("  ", "Breath Rate", breath_rate_);
  LOG_SENSOR("  ", "Temperature", temperature_);
  LOG_SENSOR("  ", "Humidity (experimental on Pro)", humidity_);
}

void SenseU::setup() {
  std::string addr = parent_->address_str();
  addr.erase(std::remove(addr.begin(), addr.end(), ':'), addr.end());
  base_service_uuid_ = espbt::ESPBTUUID::from_raw(BASE_SERVICE_UUID_PREFIX + addr);
  data_char_1_uuid_ = espbt::ESPBTUUID::from_raw(DATA_CHAR_1_PREFIX + addr);
  data_char_2_uuid_ = espbt::ESPBTUUID::from_raw(DATA_CHAR_2_PREFIX + addr);
  data_char_3_uuid_ = espbt::ESPBTUUID::from_raw(DATA_CHAR_3_PREFIX + addr);
  data_char_4_uuid_ = espbt::ESPBTUUID::from_raw(DATA_CHAR_4_PREFIX + addr);
  // Keep the original key AND packed storage layout to retain existing pairing.
  pref_ = global_preferences->make_preference<SenseUStorage>(fnv1_hash("senseu_state_" + addr), true);
  if (!pref_.load(&pref_storage_)) pref_storage_ = {};
  if (baby_code_.size() == 12) {
    pref_storage_.paired = true;
    pref_storage_.configured = true;
    auto hex = [](char c) -> uint8_t { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (size_t i = 0; i < 6; i++)
      pref_storage_.baby_code[i] = (hex(baby_code_[2*i]) << 4) | hex(baby_code_[2*i+1]);
  }
  ESP_LOGI(TAG, "Pairing credentials: %s", pref_storage_.paired ? "available" : "not stored");
  invalidate_data_();
  if (connected_sensor_) connected_sensor_->publish_state(false);
  if (status_) status_->publish_state("Setting up");
}

void SenseU::invalidate_data_() {
  fresh_ = false;
  for (auto *s : {breath_rate_, temperature_, humidity_, battery_level_, activity_, signal_strength_})
    if (s) s->publish_state(NAN);
  if (posture_) posture_->publish_state("Unavailable");
  if (charge_status_) charge_status_->publish_state("Unavailable");
  if (data_fresh_) data_fresh_->publish_state(false);
  for (auto *s : {wearing_, breath_alarm_, posture_alarm_, temperature_alarm_, battery_alarm_})
    if (s) s->invalidate_state();
  breath_alert_bits_ = posture_alert_bits_ = temperature_alert_bits_ = 0;
}

void SenseU::reset_connection_() {
  session_active_ = authenticated_ = auth_sent_ = poll_ready_ = false;
  notify_ready_ = 0;
  char_handle_1_ = char_handle_2_ = char_handle_3_ = char_handle_4_ = 0;
  cccd_1_ = cccd_2_ = cccd_3_ = cccd_4_ = 0;
  node_state = espbt::ClientState::IDLE;
  invalidate_data_();
  if (connected_sensor_) connected_sensor_->publish_state(false);
}

void SenseU::fail_connection_(const char *reason) {
  ESP_LOGW(TAG, "%s", reason);
  failed_ = true;
  session_active_ = authenticated_ = false;
  invalidate_data_();
  if (connected_sensor_) connected_sensor_->publish_state(false);
  if (status_) status_->publish_state(reason);
  // BLEClient manages reconnect. Do not erase credentials or automatically re-pair.
  parent_->disconnect();
}

void SenseU::mark_data_received_(bool snapshot) {
  if (pro_mode_ && !snapshot) return;  // Pro freshness requires a complete BA snapshot.
  last_data_ = millis();
  if (!fresh_) {
    fresh_ = true;
    if (data_fresh_) data_fresh_->publish_state(true);
    if (status_) status_->publish_state("Receiving data");
  }
}

void SenseU::loop() {
  const uint32_t now = millis();
  if (session_active_ && !authenticated_ && now - connection_started_ >= connection_timeout_) {
    fail_connection_("Authentication timeout");
    return;
  }
  if (!authenticated_) return;
  // Never interleave the first poll with the settings chain. If it stalls,
  // permit read-only polling after 20s, matching the reference driver's fallback.
  if (pro_mode_ && !poll_ready_ && now - authenticated_at_ >= 20000) {
    ESP_LOGW(TAG, "Initialization reply timeout; trying read-only snapshots");
    poll_ready_ = true;
  }
  if (pro_mode_ && poll_ready_ && now - last_poll_ >= update_interval_) {
    last_poll_ = now;
    if (!write_char(GET_BABY_DATA)) return;
  }
  const uint32_t since = fresh_ ? last_data_ : authenticated_at_;
  if (now - since >= data_timeout_) {
    fail_connection_("Data timeout");
  }
}

bool SenseU::discover_characteristics() {
  auto find = [this](const espbt::ESPBTUUID &uuid, uint16_t &handle, uint16_t &cccd) {
    auto *chr = parent_->get_characteristic(base_service_uuid_, uuid);
    if (!chr) return false;
    handle = chr->handle;
    auto *desc = parent_->get_config_descriptor(handle);
    if (!desc || desc->uuid.get_uuid().len != ESP_UUID_LEN_16 ||
        desc->uuid.get_uuid().uuid.uuid16 != ESP_GATT_UUID_CHAR_CLIENT_CONFIG) return false;
    cccd = desc->handle;
    return true;
  };
  // Pro uses CHAR_1/2/4. CHAR_3 is not required by the reference client.
  return find(data_char_1_uuid_, char_handle_1_, cccd_1_) &&
         find(data_char_2_uuid_, char_handle_2_, cccd_2_) &&
         (pro_mode_ || find(data_char_3_uuid_, char_handle_3_, cccd_3_)) &&
         find(data_char_4_uuid_, char_handle_4_, cccd_4_);
}

void SenseU::enable_notifications() {
  const uint16_t handles[] = {char_handle_1_, char_handle_2_, char_handle_3_, char_handle_4_};
  for (size_t i = 0; i < 4; i++) {
    if (!handles[i] || (i != 0 && !pref_storage_.paired)) continue;
    const auto result = esp_ble_gattc_register_for_notify(parent_->get_gattc_if(), parent_->get_remote_bda(), handles[i]);
    if (result != ESP_OK) {
      fail_connection_("Notification registration failed");
      return;
    }
  }
}

void SenseU::set_power_switch(bool state) {
  if (!preserve_monitoring_settings_ && authenticated_)
    write_char(state ? POWER_ON : POWER_OFF);
  parent_->set_enabled(state);
  power_state_ = state;
}

bool SenseU::write_char(WRITE_REQ cmd) {
  uint8_t data[20]{};
  const uint8_t *ptr = data;
  uint16_t len = 20;
  uint16_t handle = char_handle_4_;
  auto write_type = ESP_GATT_WRITE_TYPE_NO_RSP;
  const uint32_t now = millis();
  // Keep the existing successful pairing/authentication format. Pro format
  // changes should be made only against a captured failing exchange.
  switch (cmd) {
    case UID_DATA:
      ptr = SET_UID_DATA; len = sizeof(SET_UID_DATA); handle = char_handle_1_; break;
    case REGISTER_TYPE:
      len = 10; handle = char_handle_1_;
      data[0] = 0x68;
      data[1] = now >> 24; data[2] = now >> 16; data[3] = now >> 8; data[4] = now;
      data[5] = 1; data[7] = 0x32; data[8] = 3;
      break;
    case RECONNECTION_TYPE:
      len = 15; handle = char_handle_1_;
      data[0] = 0x70; memcpy(data + 1, pref_storage_.baby_code, 6);
      data[7] = now >> 24; data[8] = now >> 16; data[9] = now >> 8; data[10] = now;
      break;
    case GET_BATCH: data[0] = 0xC0; data[1] = 1; break;
    case GET_BABY_DATA:
      if (!pro_mode_ || !authenticated_) return false;
      data[0] = 0xBA; write_type = ESP_GATT_WRITE_TYPE_RSP; break;
    case LEANING_TYPE:
    case POWER_ON:
    case POWER_OFF:
      if (preserve_monitoring_settings_) return false;
      len = 5; data[0] = 0xF5; data[1] = cmd == POWER_OFF ? 0xD2 : 0xF2;
      data[2] = 0x32; data[3] = 3; break;
    case TEMP_ALARM:
      if (preserve_monitoring_settings_) return false;
      len = 6; data[0] = 0xB2; data[2] = 0x68; data[3] = 1; data[4] = 0xC8; break;
    case KICKING_ALARM:
      if (preserve_monitoring_settings_) return false;
      len = 5; data[0] = 0xB3; data[2] = 0x0F; data[3] = 3; break;
    case BREATH_ALARM:
      if (preserve_monitoring_settings_) return false;
      len = 3; data[0] = 0xB0; data[1] = 1; data[2] = 0x19; break;
    default: return false;
  }
  if (!handle || !session_active_) return false;
  ESP_LOGD(TAG, "TX command 0x%02X (%u bytes)", ptr[0], len);  // Never log credentials.
  const auto result = esp_ble_gattc_write_char(parent_->get_gattc_if(), parent_->get_conn_id(), handle,
      len, const_cast<uint8_t *>(ptr), write_type, ESP_GATT_AUTH_REQ_NONE);
  if (result != ESP_OK) {
    fail_connection_("GATT write failed");
    return false;
  }
  return true;
}

void SenseU::store_preferences() {
  if (!pref_.save(&pref_storage_)) ESP_LOGW(TAG, "Error storing pairing preferences");
}

void SenseU::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                               esp_ble_gattc_cb_param_t *param) {
  if (parent_->get_gattc_if() != gattc_if) return;
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status != ESP_GATT_OK) return;
      reset_connection_();
      failed_ = false;
      session_active_ = true;
      connection_started_ = millis();
      if (!power_state_) { parent_->set_enabled(false); return; }
      if (status_) status_->publish_state("Discovering services");
      break;
    case ESP_GATTC_CLOSE_EVT:
    case ESP_GATTC_DISCONNECT_EVT:
      // Parent may already have cleared conn_id before dispatching CLOSE_EVT.
      reset_connection_();
      if (!failed_ && status_) status_->publish_state("Disconnected");
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
      if (!session_active_) return;
      if (param->search_cmpl.status != ESP_GATT_OK || !discover_characteristics()) {
        fail_connection_("Required GATT characteristic/descriptor missing");
        return;
      }
      if (status_) status_->publish_state("Subscribing notifications");
      enable_notifications();
      break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (!session_active_) return;
      if (param->reg_for_notify.status != ESP_GATT_OK) fail_connection_("Notification registration failed");
      // BLEClient writes the CCCD. Keep services alive until ALL required
      // descriptors are acknowledged, instead of releasing on the first callback.
      break;
    case ESP_GATTC_WRITE_DESCR_EVT: {
      if (!session_active_ || auth_sent_) return;
      if (param->write.status != ESP_GATT_OK) { fail_connection_("Notification subscription failed"); return; }
      if (param->write.handle == cccd_1_) notify_ready_ |= 1;
      if (param->write.handle == cccd_2_) notify_ready_ |= 2;
      if (param->write.handle == cccd_3_) notify_ready_ |= 4;
      if (param->write.handle == cccd_4_) notify_ready_ |= 8;
      const uint8_t required = !pref_storage_.paired ? 1 : pro_mode_ ? 11 : 15;
      if ((notify_ready_ & required) == required) {
        node_state = espbt::ClientState::ESTABLISHED;
        auth_sent_ = true;
        if (status_) status_->publish_state(pref_storage_.paired ? "Authenticating" : "Pairing");
        write_char(pref_storage_.paired ? RECONNECTION_TYPE : UID_DATA);
      }
      break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
      if (session_active_ && param->write.status != ESP_GATT_OK) fail_connection_("GATT write rejected");
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (session_active_) handle_notification_(param->notify.handle, param->notify.value, param->notify.value_len);
      break;
    default: break;
  }
}

void SenseU::publish_posture_(uint8_t posture) {
  if (posture_) posture_->publish_state(protocol::posture_name(posture, pro_mode_));
}
void SenseU::publish_charge_(uint8_t charge) {
  if (charge_status_) charge_status_->publish_state(charge == 0 ? "Not charging" : charge == 1 ? "Charging" :
                                                   charge == 2 ? "Full" : "Unknown");
}

void SenseU::handle_notification_(uint16_t handle, const uint8_t *data, size_t size) {
  if (!size) return;
  ESP_LOGD(TAG, "RX handle=0x%X command=0x%02X length=%u", handle, data[0], (unsigned) size);
  if (handle == char_handle_1_) {
    if (size < 2) return;
    if (data[0] == 0x69 && !pref_storage_.paired) {
      if (data[1] != 0) { fail_connection_("Pairing required: enable device pairing mode"); return; }
      write_char(REGISTER_TYPE);
    } else if (data[0] == 0x68 && !pref_storage_.paired) {
      if (data[1] != 0) { fail_connection_("Pairing rejected"); return; }
      if (size < 8) { fail_connection_("Invalid pairing response"); return; }
      memcpy(pref_storage_.baby_code, data + 2, 6);
      pref_storage_.paired = true;
      pref_storage_.configured = false;
      store_preferences();  // Save AFTER copying the complete code, before disconnect.
      global_preferences->sync();
      ESP_LOGI(TAG, "Pairing saved; reconnecting for authentication");
      parent_->disconnect();
    } else if (data[0] == 0x70 && pref_storage_.paired && !authenticated_) {
      if (data[1] != 0) { fail_connection_("Authentication failed: check baby_code"); return; }
      authenticated_ = true;
      authenticated_at_ = last_poll_ = millis();
      ESP_LOGI(TAG, "Connected successfully!");
      if (connected_sensor_) connected_sensor_->publish_state(true);
      if (status_) status_->publish_state("Authenticated, waiting for data");
      write_char(GET_BATCH);
    }
    return;
  }
  if (!authenticated_) return;
  if (handle == char_handle_4_) {
    if (data[0] == 0xBA && pro_mode_) {
      protocol::Snapshot snapshot{};
      if (!protocol::decode_snapshot(data, size, snapshot)) {
        ESP_LOGW(TAG, "Ignoring short snapshot (%u bytes)", (unsigned) size);
        return;
      }
      if (temperature_) temperature_->publish_state(snapshot.temperature);
      if (humidity_) humidity_->publish_state(snapshot.humidity);
      if (breath_rate_) breath_rate_->publish_state(snapshot.breath);
      if (battery_level_) battery_level_->publish_state(snapshot.battery);
      if (activity_) activity_->publish_state(snapshot.activity);
      if (signal_strength_) signal_strength_->publish_state(snapshot.rssi);
      if (wearing_) wearing_->publish_state(snapshot.wearing);
      publish_posture_(snapshot.posture);
      publish_charge_(snapshot.charge);
      mark_data_received_(true);
      return;
    }
    switch (data[0]) {
      case 0xC0:
        if (preserve_monitoring_settings_) poll_ready_ = true;
        else write_char(POWER_ON);
        return;
      case 0xF5:
        if (!preserve_monitoring_settings_ && !pref_storage_.configured) write_char(TEMP_ALARM);
        else poll_ready_ = true;
        return;
      case 0xB2: if (!preserve_monitoring_settings_) write_char(KICKING_ALARM); return;
      case 0xB3: if (!preserve_monitoring_settings_) write_char(BREATH_ALARM); return;
      case 0xB0:
        if (!preserve_monitoring_settings_) { pref_storage_.configured = true; store_preferences(); }
        poll_ready_ = true;
        return;
      default: break;
    }
  }
  if (handle != char_handle_2_ && handle != char_handle_3_ && handle != char_handle_4_) return;
  if (size < 2) return;
  const uint8_t record = (data[0] >> 3) & 0x1F;
  const uint8_t type = ((uint16_t(data[0]) << 8 | data[1]) >> 6) & 0x1F;
  const size_t minimum = protocol::record_min_length(record, type);
  if (!minimum || size < minimum) return;
  if (record == 6) {
    if (type == 1) {
      if (battery_level_) battery_level_->publish_state(data[6] <= 100 ? float(data[6]) : NAN);
      if (battery_alarm_) battery_alarm_->publish_state(data[5] != 0);
    } else if (wearing_) {
      wearing_->publish_state(pro_mode_ ? type == 3 : type == 4);
    }
  } else if (record == 8) {
    switch (type) {
      case 1: {
        int raw = data[5] | (uint16_t(data[6]) << 8);
        if (raw >= 32768) raw = 32768 - raw;
        const float temp = raw / 10.0f;
        if (temperature_) temperature_->publish_state(!pro_mode_ || (temp > 10 && temp < 50) ? temp : NAN);
        // The Pro reference only identifies humidity in BA snapshots.
        if (!pro_mode_ && humidity_) humidity_->publish_state(data[7] <= 100 ? float(data[7]) : NAN);
        break;
      }
      case 2: {
        const uint8_t mode = data[5];
        const bool active = data[6] != 0;
        const uint8_t b = protocol::breath_alert_bit(mode, pro_mode_);
        const uint8_t p = protocol::posture_alert_bit(mode, pro_mode_);
        const uint8_t t = protocol::temperature_alert_bit(mode);
        if (b) { protocol::update_alert_bits(breath_alert_bits_, b, active);
                 if (breath_alarm_) breath_alarm_->publish_state(breath_alert_bits_ != 0); }
        if (p) { protocol::update_alert_bits(posture_alert_bits_, p, active);
                 if (posture_alarm_) posture_alarm_->publish_state(posture_alert_bits_ != 0); }
        if (t) { protocol::update_alert_bits(temperature_alert_bits_, t, active);
                 if (temperature_alarm_) temperature_alarm_->publish_state(temperature_alert_bits_ != 0); }
        ESP_LOGI(TAG, "Device alert mode=%u active=%s (no ACK sent)", mode, YESNO(active));
        if (pro_mode_ && mode == 81 && active) publish_charge_(2);
        return;  // An alert is not evidence of fresh measurements.
      }
      case 4:
        publish_posture_(data[5]);
        if (pro_mode_ && data[5] == 5) publish_charge_(1);
        break;
      case 5: if (breath_rate_) breath_rate_->publish_state(data[5] < 200 ? float(data[5]) : NAN); break;
      case 7: if (activity_) activity_->publish_state(data[5]); break;
    }
  }
  mark_data_received_();
}

}  // namespace senseu
}  // namespace esphome
#endif
