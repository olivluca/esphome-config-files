#include "openhaystack.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <esp_bt_main.h>
#include <esp_bt.h>
#include <freertos/task.h>
#include <esp_gap_ble_api.h>
#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#ifdef USE_ARDUINO
#include <esp32-hal-bt.h>
#endif

namespace esphome {
namespace openhaystack {

static const char *const TAG = "openhaystack";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static esp_ble_adv_params_t ble_adv_params = {
    .adv_int_min = 0x0640, // 1s
    .adv_int_max = 0x0C80, // 2s
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

void OpenHaystack::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenHaystack %d Keys, switching interval %d seconds_, current key index %d, %ssaving index to flash", this->advertising_keys_.size(), this->interval_, this->current_key_,
    this->save_key_index_ ? "" : "NOT ");
  for (int i=0; i<this->advertising_keys_.size(); i++) {
    std::array<uint8_t, 6> mac;
    for (int j=0; j<6; j++) mac[j]=this->advertising_keys_[i][j];
    mac[0] |= 0b11000000;
    ESP_LOGCONFIG(TAG,
                "  Advertising Key %d -> MAC %s KEY %s", i, format_hex_pretty(mac.data(),6).c_str(), format_hex_pretty(this->advertising_keys_[i].data(), this->advertising_keys_[i].size()).c_str()
    );
  }
}

void OpenHaystack::setup() {
  ESP_LOGCONFIG(TAG, "Setting up OpenHaystack device...");
  this->current_key_=0;
  this->curr_key_saver_ = global_preferences->make_preference<int>(fnv1_hash("openhaystack_current_key_"));
  if (this->save_key_index_) {
    if (this->curr_key_saver_.load(&this->current_key_))
    {
      this->current_key_++;
      if (this->current_key_>=this->advertising_keys_.size() || this->current_key_<0)
        this->current_key_=0;
      this->curr_key_saver_.save(&this->current_key_);
    } else {
      this->current_key_=0;
    }
  }
  ble_setup();
  select_key();
  this->lastmillis_=millis();
  this->seconds_=this->interval_;
}

void OpenHaystack::loop() {
  uint32_t curmillis = millis();
  if (curmillis - this->lastmillis_ >= 1000) {
    this->lastmillis_=curmillis;
    if (this->advertising_keys_.size()>1) {
      if (--this->seconds_<=0) {
        this->seconds_ = this->interval_;
        this->current_key_++;
        if (this->current_key_>=this->advertising_keys_.size()) this->current_key_=0;
        select_key();
      if (this->save_key_index_)
          this->curr_key_saver_.save(&this->current_key_);
      }
    }
    if (++this->s_>59) {
      this->s_=0;
      uint64_t secs = esp_timer_get_time() / 1000000L;
      int minutes = secs / 60;
      int hours = minutes / 60;
      minutes -= hours*60;
      int days = hours / 24;
      hours -= days*24;
      ESP_LOGD(TAG,"uptime %d days %d hours %d minutes", days, hours, minutes);
    }
  }
}

float OpenHaystack::get_setup_priority() const { return setup_priority::BLUETOOTH; }

void OpenHaystack::select_key() {
  esp_err_t err;

  set_addr_from_key(this->random_address_, this->advertising_keys_[this->current_key_].data());
  ESP_LOGI(TAG, "Switching to advertising key %d MAC %s", this->current_key_, format_hex_pretty(this->random_address_,6).c_str());
  set_payload_from_key(this->adv_data_, this->advertising_keys_[this->current_key_].data());
  esp_ble_gap_stop_advertising();
  err = esp_ble_gap_set_rand_addr(this->random_address_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_set_rand_addr failed: %s", esp_err_to_name(err));
    return;
  }
  esp_ble_gap_config_adv_data_raw((uint8_t *) &this->adv_data_, sizeof(this->adv_data_));
}

void OpenHaystack::set_addr_from_key(esp_bd_addr_t addr, uint8_t *public_key) {
  addr[0] = public_key[0] | 0b11000000;
  addr[1] = public_key[1];
  addr[2] = public_key[2];
  addr[3] = public_key[3];
  addr[4] = public_key[4];
  addr[5] = public_key[5];
}

void OpenHaystack::set_payload_from_key(uint8_t *payload, uint8_t *public_key) {
  /* copy last 22 bytes */
  memcpy(&payload[7], &public_key[6], 22);
  /* append two bits of public key */
  payload[29] = public_key[0] >> 6;
}

void OpenHaystack::ble_setup() {
  // Initialize non-volatile storage for the bluetooth controller
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
    return;
  }

#ifdef USE_ARDUINO
  if (!btStart()) {
    ESP_LOGE(TAG, "btStart failed: %d", esp_bt_controller_get_status());
    return;
  }
#else
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    // start bt controller
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
      esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      err = esp_bt_controller_init(&cfg);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(err));
        return;
      }
      while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
        ;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
      err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
        return;
      }
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
      ESP_LOGE(TAG, "esp bt controller enable failed");
      return;
    }
  }
#endif

  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

  err = esp_bluedroid_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", err);
    return;
  }
  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", err);
    return;
  }

  err = esp_ble_gap_register_callback(OpenHaystack::gap_event_handler);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %d", err);
    return;
  }
}

void OpenHaystack::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  esp_err_t err;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
      err = esp_ble_gap_start_advertising(&ble_adv_params);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %d", err);
      } else {
        ESP_LOGD(TAG, "BLE started advertising successfully");
      }
      break;
    }
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: {
      err = param->adv_start_cmpl.status;
      if (err != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "BLE adv start failed: %s", esp_err_to_name(err));
      }
      break;
    }
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: {
      err = param->adv_start_cmpl.status;
      if (err != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "BLE adv stop failed: %s", esp_err_to_name(err));
      } else {
        ESP_LOGD(TAG, "BLE stopped advertising successfully");
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace openhaystack
}  // namespace esphome

#endif
