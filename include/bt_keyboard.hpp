// Copyright (c) 2020, 2025 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.
//
// -----
//
// Original code from the bluetooth/esp_hid_host example of ESP-IDF license:
//
// Copyright 2017-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <forward_list>
#include <memory>
#include <string>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_hid_common.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

std::ostream &operator<<(std::ostream &os, const esp_bd_addr_t &addr);
std::ostream &operator<<(std::ostream &os, const esp_bt_uuid_t &uuid);

/**
 * @brief Bluetooth Keyboard class for handling HID keyboard devices
 *
 * This class provides functionality to connect and interact with Bluetooth HID keyboard devices.
 * It supports both classic Bluetooth and BLE (Bluetooth Low Energy) connections.
 *
 * Features:
 * - Device scanning and connection
 * - Key event handling
 * - Pairing management
 * - Battery level monitoring
 * - Connection state tracking
 * - ASCII character input support
 *
 * @note The class uses ESP32's Bluetooth stack and requires appropriate configs to be enabled
 *
 * Key components:
 * - Key modifiers handling (Ctrl, Shift, Alt, Meta)
 * - Callback support for pairing and connection events
 * - Queue-based event system for key inputs
 * - Support for both BT and BLE scan results
 *
 * Configuration dependent features:
 * - CONFIG_BT_HID_HOST_ENABLED: Classic Bluetooth HID support
 * - CONFIG_BT_BLE_ENABLED: BLE HID support
 *
 * @see KeyModifier for supported modifier keys
 * @see KeyInfo for key event data structure
 */
class BTKeyboard {
public:
  typedef void PairingHandler(uint32_t code);
  typedef void GotConnectionHandler();
  typedef void LostConnectionHandler();

  const uint8_t KEY_CAPS_LOCK = 0x39;

  enum class KeyModifier : uint8_t {
    L_CTRL  = 0x01,
    L_SHIFT = 0x02,
    L_ALT   = 0x04,
    L_META  = 0x08,
    R_CTRL  = 0x10,
    R_SHIFT = 0x20,
    R_ALT   = 0x40,
    R_META  = 0x80
  };

  const uint8_t CTRL_MASK  = ((uint8_t)KeyModifier::L_CTRL) | ((uint8_t)KeyModifier::R_CTRL);
  const uint8_t SHIFT_MASK = ((uint8_t)KeyModifier::L_SHIFT) | ((uint8_t)KeyModifier::R_SHIFT);
  const uint8_t ALT_MASK   = ((uint8_t)KeyModifier::L_ALT) | ((uint8_t)KeyModifier::R_ALT);
  const uint8_t META_MASK  = ((uint8_t)KeyModifier::L_META) | ((uint8_t)KeyModifier::R_META);

  static const uint8_t MAX_KEY_DATA_SIZE = 20;
  struct KeyInfo {
    uint8_t     size;
    uint8_t     keys[MAX_KEY_DATA_SIZE];
    KeyModifier modifier;
  };

  BTKeyboard() : num_bt_scan_results_(0), num_ble_scan_results_(0), caps_lock_(false) {}

  bool setup(PairingHandler        *pairing_handler         = nullptr,
             GotConnectionHandler  *got_connection_handler  = nullptr,
             LostConnectionHandler *lost_connection_handler = nullptr);
  void devices_scan(int seconds_wait_time = 5);

  inline uint8_t get_battery_level() { return battery_level_; }
  inline bool    is_connected() { return connected_; }
  inline bool    wait_for_low_event(KeyInfo &inf, TickType_t duration = portMAX_DELAY) {
    return xQueueReceive(event_queue_, &inf, duration);
  }

  char        wait_for_ascii_char(bool forever = true);
  inline char get_ascii_char() { return wait_for_ascii_char(false); }
  void        show_bonded_devices();
  void        remove_all_bonded_devices();

private:
  static constexpr char const *TAG          = "BTKeyboard";

  static const esp_bt_mode_t HIDH_IDLE_MODE = (esp_bt_mode_t)0x00;
  static const esp_bt_mode_t HIDH_BLE_MODE  = (esp_bt_mode_t)0x01;
  static const esp_bt_mode_t HIDH_BT_MODE   = (esp_bt_mode_t)0x02;
  static const esp_bt_mode_t HIDH_BTDM_MODE = (esp_bt_mode_t)0x03;

#if CONFIG_BT_HID_HOST_ENABLED
  #if CONFIG_BT_BLE_ENABLED
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BTDM_MODE;
  #else
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BT_MODE;
  #endif
#elif CONFIG_BT_BLE_ENABLED
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BLE_MODE;
#else
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_IDLE_MODE;
#endif

  static SemaphoreHandle_t bt_hidh_cb_semaphore_;
  static SemaphoreHandle_t ble_hidh_cb_semaphore_;

  struct esp_hid_scan_result_t {
    esp_bd_addr_t       bda;
    std::string         name;
    int8_t              rssi;
    esp_hid_usage_t     usage;
    esp_hid_transport_t transport; // BT, BLE or USB

    union {
      struct {
        esp_bt_cod_t  cod;
        esp_bt_uuid_t uuid;
      } bt;
      struct {
        esp_ble_addr_type_t addr_type;
        uint16_t            appearance;
      } ble;
    };
  };

  typedef std::forward_list<std::unique_ptr<esp_hid_scan_result_t>> ScanResult;

  ScanResult bt_scan_results_;
  ScanResult ble_scan_results_;

  size_t num_bt_scan_results_;
  size_t num_ble_scan_results_;

  QueueHandle_t event_queue_;
  int8_t        battery_level_;
  bool          key_avail_[MAX_KEY_DATA_SIZE];
  char          last_ch_;
  TickType_t    repeat_period_;
  bool          caps_lock_;

  static const char *gap_bt_prop_type_names_[];
  static const char *ble_gap_evt_names_[];
  static const char *bt_gap_evt_names_[];
  static const char *ble_addr_type_names_[];

  static const char shift_trans_dict_[];

  static BTKeyboard            *bt_keyboard_;
  static PairingHandler        *pairing_handler_;
  static GotConnectionHandler  *got_connection_handler_;
  static LostConnectionHandler *lost_connection_handler_;
  static bool                   connected_;

  static void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id,
                            void *event_data);

  auto retrieve_bonded_devices() -> std::pair<std::shared_ptr<esp_ble_bond_dev_t[]>, int>;

  static void bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

  static const char *ble_addr_type_str(esp_ble_addr_type_t ble_addr_type);
  static const char *ble_gap_evt_str(uint8_t event);
  static const char *bt_gap_evt_str(uint8_t event);
  static const char *ble_key_type_str(esp_ble_key_type_t key_type);

  void handle_bt_device_result(esp_bt_gap_cb_param_t *param);
  void handle_ble_device_result(esp_ble_gap_cb_param_t *param);

  esp_hid_scan_result_t *find_scan_result(esp_bd_addr_t bda, ScanResult &results);

  void add_bt_scan_result(esp_bd_addr_t bda, esp_bt_cod_t *cod, esp_bt_uuid_t *uuid, uint8_t *name,
                          uint8_t name_len, int rssi);

  void add_ble_scan_result(esp_bd_addr_t bda, esp_ble_addr_type_t addr_type, uint16_t appearance,
                           uint8_t *name, uint8_t name_len, int rssi);

  void print_uuid(esp_bt_uuid_t *uuid);

  esp_err_t start_ble_scan(uint32_t seconds);
  esp_err_t start_bt_scan(uint32_t seconds);
  esp_err_t esp_hid_scan(uint32_t seconds, size_t *num_results, ScanResult &results);

  inline void set_battery_level(uint8_t level) { battery_level_ = level; }
  inline void set_connected(bool connected) {
    connected_ = connected;
    if (connected) {
      if (got_connection_handler_ != nullptr) {
        (*got_connection_handler_)();
      }
    } else if (lost_connection_handler_ != nullptr) {
      (*lost_connection_handler_)();
    }
  }

  void push_key(uint8_t *keys, uint8_t size);
};
