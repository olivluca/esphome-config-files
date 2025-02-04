#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#ifdef USE_ESP32

#include <esp_gap_ble_api.h>
#include <vector>

namespace esphome {
namespace openhaystack {

class OpenHaystack : public Component {
 public:
  explicit OpenHaystack() { this->advertising_keys_.reserve(100); }
  void add_adv_key(const std::array<uint8_t, 28> &advertising_key) { this->advertising_keys_.push_back(advertising_key); }
  void set_switch_key_interval(int interval) { this->interval_ = interval; }
  void set_save_key_index(bool value) { this->save_key_index_ = value; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void select_key();
  static void set_addr_from_key(esp_bd_addr_t addr, uint8_t *public_key);
  static void set_payload_from_key(uint8_t *payload, uint8_t *public_key);
  static void ble_setup();

  ESPPreferenceObject curr_key_saver_;

  std::vector<std::array<uint8_t, 28>> advertising_keys_;
  int current_key_;
  int interval_=3600;
  uint32_t lastmillis_;
  int s_=0;
  int seconds_;
  bool save_key_index_ = false;
  esp_bd_addr_t random_address_ = {0xFF, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t adv_data_[31] = {
    0x1e, /* Length (30) */
    0xff, /* Manufacturer Specific Data (type 0xff) */
    0x4c, 0x00, /* Company ID (Apple) */
    0x12, 0x19, /* Offline Finding type and length */
    0x00, /* State */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, /* First two bits */
    0x00, /* Hint (0x00) */
  };
};

}  // namespace openhaystack
}  // namespace esphome

#endif
