#include "components/senseu/senseu.h"
#include <cassert>
#include <cmath>
using namespace esphome;
using namespace esphome::senseu;

void event(SenseU &s, esp_gattc_cb_event_t e, uint16_t handle=0) {
 esp_ble_gattc_cb_param_t p{};p.write.handle=handle;s.gattc_event_handler(e,1,&p);
}
void notify(SenseU &s, uint16_t handle, std::vector<uint8_t> data) {
 esp_ble_gattc_cb_param_t p{};p.notify={handle,data.data(),uint16_t(data.size())};
 s.gattc_event_handler(ESP_GATTC_NOTIFY_EVT,1,&p);
}
void discover(SenseU &s) {event(s,ESP_GATTC_OPEN_EVT);event(s,ESP_GATTC_SEARCH_CMPL_EVT);}
void descriptors(SenseU &s) {
 event(s,ESP_GATTC_WRITE_DESCR_EVT,0x12);
 event(s,ESP_GATTC_WRITE_DESCR_EVT,0x22);
 event(s,ESP_GATTC_WRITE_DESCR_EVT,0x42);
}
int main() {
 // No device or transport is touched; events below are synthetic.
 ble_client::BLEClient parent;
 SenseU s;s.parent_=&parent;s.set_pro_mode(true);s.set_preserve_monitoring_settings(true);
 sensor::Sensor breath,temp;binary_sensor::BinarySensor connected,fresh,alarm,wearing;
 text_sensor::TextSensor status;
 s.set_breath_rate(&breath);s.set_temperature(&temp);s.set_connected_sensor(&connected);
 s.set_data_fresh(&fresh);s.set_breath_alarm(&alarm);s.set_wearing(&wearing);s.set_status_sensor(&status);
 s.setup(); assert(!alarm.valid && !connected.state);
 discover(s); assert(subscriptions.size()==1 && subscriptions[0]==0x11);
 event(s,ESP_GATTC_WRITE_DESCR_EVT,0x12); assert(writes.back().bytes[0]==0x69);
 notify(s,0x11,{0x69,1});assert(parent.disconnected && status.state.find("Pairing required")!=std::string::npos);
 event(s,ESP_GATTC_CLOSE_EVT); assert(!connected.state && !fresh.state);
 // The failure status survives CLOSE, rather than returning to Connecting.
 assert(status.state.find("Pairing required")!=std::string::npos);
 discover(s);event(s,ESP_GATTC_WRITE_DESCR_EVT,0x12);
 notify(s,0x11,{0x69,0});assert(writes.back().bytes[0]==0x68);
 notify(s,0x11,{0x68,0,1,2,3,4,5,6});assert(stored);
 for(int i=0;i<6;i++)assert(persisted[i+2]==i+1);
 event(s,ESP_GATTC_CLOSE_EVT);parent.disconnected=false;
 // Reboot with flash-stored credentials, no YAML baby_code.
 SenseU restored;restored.parent_=&parent;restored.set_pro_mode(true);restored.set_preserve_monitoring_settings(true);
 restored.set_breath_rate(&breath);restored.set_temperature(&temp);restored.set_connected_sensor(&connected);
 restored.set_data_fresh(&fresh);restored.set_breath_alarm(&alarm);restored.set_wearing(&wearing);
 restored.set_status_sensor(&status);restored.setup();
 writes.clear();subscriptions.clear();discover(restored);
 assert(subscriptions.size()==3);
 event(restored,ESP_GATTC_WRITE_DESCR_EVT,0x12);assert(writes.empty());
 event(restored,ESP_GATTC_WRITE_DESCR_EVT,0x22);assert(writes.empty());
 event(restored,ESP_GATTC_WRITE_DESCR_EVT,0x42);assert(writes.back().bytes[0]==0x70);
 for(int i=0;i<6;i++)assert(writes.back().bytes[i+1]==i+1);
 const auto count=writes.size();event(restored,ESP_GATTC_WRITE_DESCR_EVT,0x42);assert(writes.size()==count);
 clock_ms=100;notify(restored,0x11,{0x70,0});assert(connected.state && !fresh.state);
 assert(writes.back().bytes[0]==0xC0);
 notify(restored,0x41,{0xC0});clock_ms=2100;restored.loop();
 assert(writes.back().bytes.size()==20 && writes.back().bytes[0]==0xBA && writes.back().type==ESP_GATT_WRITE_TYPE_RSP);
 notify(restored,0x41,{0xBA,0,0});assert(!fresh.state);
 notify(restored,0x41,{0xBA,0,1,0x6D,1,55,32,12,0xC2,78,0x81,2});
 assert(fresh.state && temp.state==36.5f && breath.state==32 && wearing.state);
 assert(!alarm.valid); // Snapshot has no alarm state; do not claim no alarm.
 notify(restored,0x21,{0x40,0x80,0,0,0,9,1});assert(alarm.state);
 notify(restored,0x21,{0x40,0x80,0,0,0,10,1});
 notify(restored,0x21,{0x40,0x80,0,0,0,9,0});assert(alarm.state);
 // Isolated realtime push must not make stale Pro snapshots look fresh.
 clock_ms=32000;notify(restored,0x21,{0x40,0x40,0,0,0,0x6D,1,0});
 restored.loop();assert(!parent.disconnected);
 clock_ms=32100;restored.loop();assert(parent.disconnected && !fresh.state);
 assert(std::isnan(breath.state) && !alarm.valid && !wearing.valid);
 assert(status.state=="Data timeout");
 for(const auto &w:writes) assert(w.bytes[0]!=0xF5 && w.bytes[0]!=0xB2 && w.bytes[0]!=0xB3 && w.bytes[0]!=0xB0 && w.bytes[0]!=0xF6);
 // Timeout from open when discovery/notifications never finish, including wraparound.
 event(restored,ESP_GATTC_CLOSE_EVT);parent.disconnected=false;
 clock_ms=0xFFFFFF00;event(restored,ESP_GATTC_OPEN_EVT);
 clock_ms+=30000;restored.loop();assert(parent.disconnected && status.state=="Authentication timeout");
 // Failed authentication must retain the stored code and never poll.
 event(restored,ESP_GATTC_CLOSE_EVT);parent.disconnected=false;writes.clear();
 discover(restored);descriptors(restored);
 notify(restored,0x11,{0x70,1});assert(parent.disconnected && stored);
 assert(status.state.find("Authentication failed")!=std::string::npos);
 clock_ms+=2000;restored.loop();assert(writes.size()==1);
 // Synchronous GATT write error exposes a failure, not a fake connected state.
 event(restored,ESP_GATTC_CLOSE_EVT);parent.disconnected=false;
 discover(restored);write_result=1;descriptors(restored);
 assert(parent.disconnected && !connected.state && status.state=="GATT write failed");
}
