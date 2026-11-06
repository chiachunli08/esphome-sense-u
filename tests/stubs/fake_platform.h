#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <cstring>
#ifndef USE_ESP32
#define USE_ESP32
#endif
#define PACKED __attribute__((packed))
#define ESP_LOGCONFIG(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define LOG_SENSOR(...) ((void)0)
#define YESNO(v) ((v) ? "YES" : "NO")
using esp_gatt_if_t = int;
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_GATT_OK=0, ESP_UUID_LEN_16=16, ESP_GATT_UUID_CHAR_CLIENT_CONFIG=0x2902;
constexpr int ESP_GATT_WRITE_TYPE_NO_RSP=0, ESP_GATT_WRITE_TYPE_RSP=1, ESP_GATT_AUTH_REQ_NONE=0;
enum esp_gattc_cb_event_t {
 ESP_GATTC_OPEN_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_SEARCH_CMPL_EVT,
 ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_WRITE_DESCR_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_NOTIFY_EVT
};
struct esp_ble_gattc_cb_param_t {
 struct { int status=0; } open, search_cmpl, reg_for_notify;
 struct { int status=0; uint16_t handle=0; } write;
 struct { uint16_t handle=0; uint8_t *value=nullptr; uint16_t value_len=0; } notify;
};
struct Write { uint16_t handle; std::vector<uint8_t> bytes; int type; };
inline std::vector<Write> writes;
inline std::vector<uint16_t> subscriptions;
inline int write_result=0;
inline int esp_ble_gattc_register_for_notify(int, uint8_t *, uint16_t handle) {
 subscriptions.push_back(handle); return 0;
}
inline int esp_ble_gattc_write_char(int, int, uint16_t handle, uint16_t length, uint8_t *data, int type, int) {
 writes.push_back({handle, {data,data+length},type}); return write_result;
}
namespace esphome {
inline uint32_t clock_ms=0;
inline uint32_t millis() { return clock_ms; }
inline uint32_t fnv1_hash(const std::string &) { return 123; }
namespace setup_priority { constexpr float DATA=0; }
class Component { public: virtual void setup() {} virtual void loop() {} virtual void dump_config() {}
 virtual float get_setup_priority() const { return 0; } };
inline std::array<uint8_t,8> persisted{};
inline bool stored=false;
struct ESPPreferenceObject {
 template<class T> bool load(T *data) { if (!stored) return false; memcpy(data,persisted.data(),sizeof(T)); return true; }
 template<class T> bool save(T *data) { static_assert(sizeof(T)==8); memcpy(persisted.data(),data,sizeof(T)); stored=true; return true; }
};
struct Preferences { template<class T> ESPPreferenceObject make_preference(uint32_t,bool) {return {};}
 bool sync() { return true; } };
inline Preferences preferences;
inline Preferences *global_preferences=&preferences;
namespace sensor { struct Sensor {float state=0; void publish_state(float s) {state=s;} }; }
namespace text_sensor { struct TextSensor {std::string state; void publish_state(const std::string &s) {state=s;} }; }
namespace binary_sensor { struct BinarySensor {bool state=false; bool valid=false;
 void publish_state(bool s) {state=s;valid=true;} void invalidate_state(){valid=false;} }; }
namespace esp32_ble_tracker {
 enum class ClientState { IDLE, ESTABLISHED };
 struct ESPBTUUID {
  std::string text;
  static ESPBTUUID from_raw(std::string s) { return {s}; }
  struct Raw {int len=16; struct {int uuid16=0x2902;} uuid;};
  Raw get_uuid() const {return {};}
 };
}
namespace ble_client {
struct BLECharacteristic {uint16_t handle;};
struct BLEDescriptor {uint16_t handle; esp32_ble_tracker::ESPBTUUID uuid;};
struct BLEClient {
 bool disconnected=false, enabled=true;
 uint8_t bda[6]{};
 std::map<int,BLECharacteristic> chars{{1,{0x11}},{2,{0x21}},{3,{0x31}},{4,{0x41}}};
 std::map<int,BLEDescriptor> descs{{0x11,{0x12,{}}},{0x21,{0x22,{}}},{0x31,{0x32,{}}},{0x41,{0x42,{}}}};
 const char *address_str(){return "AA:BB:CC:DD:EE:FF";}
 int get_gattc_if(){return 1;} int get_conn_id(){return 0;} uint8_t *get_remote_bda(){return bda;}
 void disconnect(){disconnected=true;} void set_enabled(bool b){enabled=b;if(!b)disconnect();}
 BLECharacteristic *get_characteristic(esp32_ble_tracker::ESPBTUUID, esp32_ble_tracker::ESPBTUUID uuid){
  int i=uuid.text[7]-'0'; if(i==5)i=4;
  auto it=chars.find(i);return it==chars.end()?nullptr:&it->second;
 }
 BLEDescriptor *get_config_descriptor(uint16_t h){auto it=descs.find(h);return it==descs.end()?nullptr:&it->second;}
};
struct BLEClientNode {
 BLEClient *parent_=nullptr;
 esp32_ble_tracker::ClientState node_state=esp32_ble_tracker::ClientState::IDLE;
 virtual void gattc_event_handler(esp_gattc_cb_event_t,int,esp_ble_gattc_cb_param_t*){}
};
}
}
