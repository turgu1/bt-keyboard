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

#define __BT_KEYBOARD__ 1
#include "bt_keyboard.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <ostream>

#define SCAN            1

#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof(*a))

#define WAIT_BT_CB()    xSemaphoreTake(bt_hidh_cb_semaphore_, portMAX_DELAY)
#define SEND_BT_CB()    xSemaphoreGive(bt_hidh_cb_semaphore_)

#define WAIT_BLE_CB()   xSemaphoreTake(ble_hidh_cb_semaphore_, portMAX_DELAY)
#define SEND_BLE_CB()   xSemaphoreGive(ble_hidh_cb_semaphore_)

SemaphoreHandle_t BTKeyboard::bt_hidh_cb_semaphore_  = nullptr;
SemaphoreHandle_t BTKeyboard::ble_hidh_cb_semaphore_ = nullptr;

const char *BTKeyboard::gap_bt_prop_type_names_[]    = {"", "BDNAME", "COD", "RSSI", "EIR"};

const char *BTKeyboard::ble_gap_evt_names_[]         = {"ADV_DATA_SET_COMPLETE",
                                                        "SCAN_RSP_DATA_SET_COMPLETE",
                                                        "SCAN_PARAM_SET_COMPLETE",
                                                        "SCAN_RESULT",
                                                        "ADV_DATA_RAW_SET_COMPLETE",
                                                        "SCAN_RSP_DATA_RAW_SET_COMPLETE",
                                                        "ADV_START_COMPLETE",
                                                        "SCAN_START_COMPLETE",
                                                        "AUTH_CMPL",
                                                        "KEY",
                                                        "SEC_REQ",
                                                        "PASSKEY_NOTIF",
                                                        "PASSKEY_REQ",
                                                        "OOB_REQ",
                                                        "LOCAL_IR",
                                                        "LOCAL_ER",
                                                        "NC_REQ",
                                                        "ADV_STOP_COMPLETE",
                                                        "SCAN_STOP_COMPLETE",
                                                        "SET_STATIC_RAND_ADDR",
                                                        "UPDATE_CONN_PARAMS",
                                                        "SET_PKT_LENGTH_COMPLETE",
                                                        "SET_LOCAL_PRIVACY_COMPLETE",
                                                        "REMOVE_BOND_DEV_COMPLETE",
                                                        "CLEAR_BOND_DEV_COMPLETE",
                                                        "GET_BOND_DEV_COMPLETE",
                                                        "READ_RSSI_COMPLETE",
                                                        "UPDATE_WHITELIST_COMPLETE"};

const char *BTKeyboard::bt_gap_evt_names_[]          = {"DISC_RES",
                                                        "DISC_STATE_CHANGED",
                                                        "RMT_SRVCS",
                                                        "RMT_SRVC_REC",
                                                        "AUTH_CMPL",
                                                        "PIN_REQ",
                                                        "CFM_REQ",
                                                        "KEY_NOTIF",
                                                        "KEY_REQ",
                                                        "READ_RSSI_DELTA",
                                                        "CONFIG_EIR_DATA",
                                                        "SET_AFH_CHANNELS",
                                                        "READ_REMOTE_NAME",
                                                        "MODE_CHG",
                                                        "REMOVE_BOND_DEV_COMPLETE",
                                                        "QOS_CMPL",
                                                        "ACL_CONN_CMPL_STAT",
                                                        "ACL_DISCONN_CMPL_STAT",
                                                        "SET_PAGE_TO",
                                                        "GET_PAGE_TO",
                                                        "ACL_PKT_TYPE_CHANGED",
                                                        "ENC_CHG_EVT",
                                                        "SET_MIN_ENC_KEY_SIZE",
                                                        "GET_DEV_NAME_CMPL"};

const char *BTKeyboard::ble_addr_type_names_[] = {"PUBLIC", "RANDOM", "RPA_PUBLIC", "RPA_RANDOM"};

const char BTKeyboard::shift_trans_dict_[] =
    "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ1!2@3#4$5%6^7&8*9(0)"
    "\r\r\033\033\b\b\t\t  -_=+[{]}\\|??;:'\"`~,<.>/?"
    "\200\200"                                          // CAPS LOC
    "\201\201\202\202\203\203\204\204\205\205\206\206"  // F1..F6
    "\207\207\210\210\211\211\212\212\213\213\214\214"  // F7..F12
    "\215\215\216\216\217\217"                          // PrintScreen ScrollLock Pause
    "\220\220\221\221\222\222\177\177"                  // Insert Home PageUp Delete
    "\223\223\224\224\225\225\226\226\227\227\230\230"; // End PageDown Right Left Dow Up

BTKeyboard                        *BTKeyboard::bt_keyboard_             = nullptr;
BTKeyboard::PairingHandler        *BTKeyboard::pairing_handler_         = nullptr;
BTKeyboard::GotConnectionHandler  *BTKeyboard::got_connection_handler_  = nullptr;
BTKeyboard::LostConnectionHandler *BTKeyboard::lost_connection_handler_ = nullptr;

bool BTKeyboard::connected_                                             = false;

/**
 * @brief Overloads the output stream operator for esp_bd_addr_t (Bluetooth address)
 *
 * Formats and outputs a Bluetooth address in the standard XX:XX:XX:XX:XX:XX format,
 * where XX represents two hexadecimal digits. The original stream formatting flags
 * are preserved after the operation.
 *
 * @param os Output stream to write to
 * @param addr Bluetooth address to format and output
 * @return std::ostream& Reference to the output stream
 */
std::ostream &operator<<(std::ostream &os, const esp_bd_addr_t &addr) {
  std::ios_base::fmtflags oldFlags = os.flags();
  char                    oldFill  = os.fill('0');
  char                    oldWidth = os.width();

  os << std::hex << std::setw(2) << +addr[0];
  for (uint i = 1; i < 6; ++i) {
    os << ':' << std::hex << std::setw(2) << +addr[i];
  }

  os.fill(oldFill);
  os.flags(oldFlags);
  os.width(oldWidth);

  os << std::dec;

  return os;
}

/**
 * @brief Stream operator overload for esp_bt_uuid_t to enable UUID printing
 *
 * Formats and outputs Bluetooth UUIDs in human-readable format:
 * - 16-bit UUID as "UUID16: 0xXXXX"
 * - 32-bit UUID as "UUID32: 0xXXXXXXXX"
 * - 128-bit UUID in standard format "UUID128: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
 *
 * Preserves stream formatting state by saving and restoring flags, fill character and width.
 *
 * @param os Output stream to write to
 * @param uuid Bluetooth UUID structure to format
 * @return Reference to the output stream
 */
std::ostream &operator<<(std::ostream &os, const esp_bt_uuid_t &uuid) {
  std::ios_base::fmtflags oldFlags = os.flags();
  char                    oldFill  = os.fill('0');
  char                    oldWidth = os.width();

  if (uuid.len == ESP_UUID_LEN_16) {
    os << "UUID16: 0x" << std::hex << std::setw(4) << uuid.uuid.uuid16;
  } else if (uuid.len == ESP_UUID_LEN_32) {
    os << "UUID32: 0x" << std::hex << std::setw(8) << uuid.uuid.uuid32;
  } else if (uuid.len == ESP_UUID_LEN_128) {
    os << "UUID128: ";
    for (uint i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) {
        os << '-';
      }
      os << std::setw(2) << std::hex << +uuid.uuid.uuid128[i];
    }
  }

  os.fill(oldFill);
  os.flags(oldFlags);
  os.width(oldWidth);

  os << std::dec;

  return os;
}

/**
 * @brief Converts BLE address type to a human-readable string
 *
 * @param ble_addr_type The Bluetooth Low Energy address type to convert
 * @return const char* String representation of the BLE address type:
 *         - One of the predefined address type names from ble_addr_type_names_
 *         - "UNKNOWN" if the address type is invalid
 */
const char *BTKeyboard::ble_addr_type_str(esp_ble_addr_type_t ble_addr_type) {
  if (ble_addr_type > BLE_ADDR_TYPE_RPA_RANDOM) {
    return "UNKNOWN";
  }
  return ble_addr_type_names_[ble_addr_type];
}

/**
 * @brief Converts BLE GAP event code to string representation
 *
 * This function maps BLE GAP event codes to their string names for debugging
 * and logging purposes. If the event code is out of range, returns "UNKNOWN".
 *
 * @param event The BLE GAP event code to convert
 * @return const char* String representation of the event code
 */
const char *BTKeyboard::ble_gap_evt_str(uint8_t event) {
  if (event >= SIZEOF_ARRAY(ble_gap_evt_names_)) {
    return "UNKNOWN";
  }
  return ble_gap_evt_names_[event];
}

/**
 * @brief Converts a Bluetooth GAP event code to its string representation
 *
 * This function takes a GAP event code and returns a human-readable string
 * describing the event. If the event code is out of bounds, returns "UNKNOWN".
 *
 * @param event The Bluetooth GAP event code to convert
 * @return const char* String representation of the GAP event
 */
const char *BTKeyboard::bt_gap_evt_str(uint8_t event) {
  if (event >= SIZEOF_ARRAY(bt_gap_evt_names_)) {
    return "UNKNOWN";
  }
  return bt_gap_evt_names_[event];
}

/**
 * @brief Converts BLE key type to its string representation
 *
 * This function takes a BLE key type enum value and returns its corresponding
 * string representation for debugging and logging purposes.
 *
 * @param key_type The BLE key type enum value to convert
 * @return const char* String representation of the key type. Returns "INVALID BLE KEY TYPE" if
 * unknown
 *
 * Possible return values:
 * - "ESP_LE_KEY_NONE"
 * - "ESP_LE_KEY_PENC"
 * - "ESP_LE_KEY_PID"
 * - "ESP_LE_KEY_PCSRK"
 * - "ESP_LE_KEY_PLK"
 * - "ESP_LE_KEY_LLK"
 * - "ESP_LE_KEY_LENC"
 * - "ESP_LE_KEY_LID"
 * - "ESP_LE_KEY_LCSRK"
 * - "INVALID BLE KEY TYPE"
 */
const char *BTKeyboard::ble_key_type_str(esp_ble_key_type_t key_type) {
  const char *key_str = nullptr;
  switch (key_type) {
    case ESP_LE_KEY_NONE:
      key_str = "ESP_LE_KEY_NONE";
      break;
    case ESP_LE_KEY_PENC:
      key_str = "ESP_LE_KEY_PENC";
      break;
    case ESP_LE_KEY_PID:
      key_str = "ESP_LE_KEY_PID";
      break;
    case ESP_LE_KEY_PCSRK:
      key_str = "ESP_LE_KEY_PCSRK";
      break;
    case ESP_LE_KEY_PLK:
      key_str = "ESP_LE_KEY_PLK";
      break;
    case ESP_LE_KEY_LLK:
      key_str = "ESP_LE_KEY_LLK";
      break;
    case ESP_LE_KEY_LENC:
      key_str = "ESP_LE_KEY_LENC";
      break;
    case ESP_LE_KEY_LID:
      key_str = "ESP_LE_KEY_LID";
      break;
    case ESP_LE_KEY_LCSRK:
      key_str = "ESP_LE_KEY_LCSRK";
      break;
    default:
      key_str = "INVALID BLE KEY TYPE";
      break;
  }

  return key_str;
}

/**
 * @brief Initializes the Bluetooth keyboard functionality.
 *
 * This function sets up both Classic Bluetooth and BLE (Bluetooth Low Energy) for HID host mode.
 * It initializes the Bluetooth controller, Bluedroid stack, and configures security parameters
 * for device pairing. The function also creates necessary queues and semaphores for handling
 * keyboard events.
 *
 * @param pairing_handler Callback handler for pairing events
 * @param got_connection_handler Callback handler for successful connection events
 * @param lost_connection_handler Callback handler for connection loss events
 *
 * @return true if setup was successful, false if any initialization step fails
 *
 * @note Only one instance of BTKeyboard is allowed
 * @note The function configures both Classic Bluetooth and BLE GAP parameters
 * @note Creates an event queue of size 10 for handling KeyInfo events
 * @note Initializes key availability array and resets last character and battery level
 *
 * @warning This function should be called only once
 */
bool BTKeyboard::setup(PairingHandler        *pairing_handler,
                       GotConnectionHandler  *got_connection_handler,
                       LostConnectionHandler *lost_connection_handler) {

  esp_err_t           ret;
  const esp_bt_mode_t mode = HID_HOST_MODE;

  if (bt_keyboard_ != nullptr) {
    ESP_LOGE(TAG, "Setup called more than once. Only one instance of BTKeyboard is allowed.");
    return false;
  }

  bt_keyboard_             = this;

  pairing_handler_         = pairing_handler;
  got_connection_handler_  = got_connection_handler;
  lost_connection_handler_ = lost_connection_handler;

  event_queue_             = xQueueCreate(10, sizeof(KeyInfo));

  if (HID_HOST_MODE == HIDH_IDLE_MODE) {
    ESP_LOGE(TAG, "Please turn on BT HID host or BLE!");
    return false;
  }

  bt_hidh_cb_semaphore_ = xSemaphoreCreateBinary();
  if (bt_hidh_cb_semaphore_ == nullptr) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    return false;
  }

  ble_hidh_cb_semaphore_ = xSemaphoreCreateBinary();
  if (ble_hidh_cb_semaphore_ == nullptr) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    vSemaphoreDelete(bt_hidh_cb_semaphore_);
    bt_hidh_cb_semaphore_ = nullptr;
    return false;
  }

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

  bt_cfg.mode                       = mode;
  bt_cfg.bt_max_acl_conn            = 3;
  bt_cfg.bt_max_sync_conn           = 3;

  if ((ret = esp_bt_controller_init(&bt_cfg))) {
    ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
    return false;
  }

  if ((ret = esp_bt_controller_enable(mode))) {
    ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
    return false;
  }

  if ((ret = esp_bluedroid_init())) {
    ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", ret);
    return false;
  }

  if ((ret = esp_bluedroid_enable())) {
    ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", ret);
    return false;
  }

  // Classic Bluetooth GAP

  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_IO;
  esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

  // Set default parameters for Legacy Pairing
  // Use variable pin, input pin code when pairing
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  esp_bt_gap_set_pin(pin_type, 0, pin_code);

  if ((ret = esp_bt_gap_register_callback(bt_gap_event_handler))) {
    ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %d", ret);
    return false;
  }

  // Allow BT devices to connect back to us
  if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE))) {
    ESP_LOGE(TAG, "esp_bt_gap_set_scan_mode failed: %d", ret);
    return false;
  }

  // BLE GAP

  if ((ret = esp_ble_gap_register_callback(ble_gap_event_handler))) {
    ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %d", ret);
    return false;
  }

  ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
  esp_hidh_config_t config = {
      .callback = hidh_callback, .event_stack_size = 4 * 1024, .callback_arg = nullptr};
  ESP_ERROR_CHECK(esp_hidh_init(&config));

  for (int i = 0; i < MAX_KEY_DATA_SIZE; i++) {
    key_avail_[i] = true;
  }

  last_ch_       = 0;
  battery_level_ = -1;
  return true;
}

/**
 * @brief Searches for a specific Bluetooth device in scan results
 *
 * @param bda Bluetooth device address to search for
 * @param results Reference to scan results container
 * @return BTKeyboard::esp_hid_scan_result_t* Pointer to scan result if found, nullptr otherwise
 *
 * Iterates through scan results looking for a device matching the provided
 * Bluetooth address. Returns the matching scan result or nullptr if not found.
 */
BTKeyboard::esp_hid_scan_result_t *BTKeyboard::find_scan_result(esp_bd_addr_t bda,
                                                                ScanResult   &results) {
  for (auto &res : results) {
    if (memcmp(bda, res->bda, sizeof(esp_bd_addr_t)) == 0) {
      return res.get();
    }
  }
  return nullptr;
}

/**
 * @brief Adds or updates a Bluetooth HID device scan result
 *
 * This method processes scan results from Bluetooth HID device discovery. If a device with
 * the same address already exists in the scan results, it updates the existing entry with
 * any new information. Otherwise, it creates a new scan result entry.
 *
 * @param bda Bluetooth device address
 * @param cod Class of Device information
 * @param uuid UUID of the device
 * @param name Device name
 * @param name_len Length of the device name
 * @param rssi Received Signal Strength Indicator
 *
 * @note The scan results are stored in bt_scan_results_ list, with the most recent
 *       results being added to the front of the list.
 *
 * @note The method maintains a count of total scan results in num_bt_scan_results_
 */
void BTKeyboard::add_bt_scan_result(esp_bd_addr_t bda, esp_bt_cod_t *cod, esp_bt_uuid_t *uuid,
                                    uint8_t *name, uint8_t name_len, int rssi) {
  esp_hid_scan_result_t *r = find_scan_result(bda, bt_scan_results_);
  if (r) {
    // Some info may come later
    if (r->name.empty() && name && name_len) {
      r->name.assign(reinterpret_cast<const char *>(name), name_len);
    }
    if (r->bt.uuid.len == 0 && uuid->len) {
      memcpy(&r->bt.uuid, uuid, sizeof(esp_bt_uuid_t));
    }
    if (rssi != 0) {
      r->rssi = rssi;
    }
    return;
  }

  auto res = std::make_unique<esp_hid_scan_result_t>();

  if (res == nullptr) {
    ESP_LOGE(TAG, "make_unique of bt_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BT;

  memcpy(res->bda, bda, sizeof(esp_bd_addr_t));
  memcpy(&res->bt.cod, cod, sizeof(esp_bt_cod_t));
  memcpy(&res->bt.uuid, uuid, sizeof(esp_bt_uuid_t));

  res->usage = esp_hid_usage_from_cod((uint32_t)cod);
  res->rssi  = rssi;
  res->name.clear();

  if (name_len && name) {
    res->name.assign(reinterpret_cast<const char *>(name), name_len);
  }

  bt_scan_results_.push_front(std::move(res));

  num_bt_scan_results_++;
}

/**
 * @brief Adds a new BLE scan result to the scan results list
 *
 * This method stores information about a discovered BLE device in the scan results list.
 * If the device is already in the list, the method returns without adding a duplicate entry.
 *
 * @param bda The Bluetooth device address
 * @param addr_type The BLE address type
 * @param appearance The BLE appearance value indicating device type
 * @param name Pointer to the device name buffer
 * @param name_len Length of the device name
 * @param rssi Received Signal Strength Indicator value
 *
 * @note The scan result is stored in a unique_ptr and moved to the front of the list
 * @note The method increments num_ble_scan_results_ counter when a new result is added
 */
void BTKeyboard::add_ble_scan_result(esp_bd_addr_t bda, esp_ble_addr_type_t addr_type,
                                     uint16_t appearance, uint8_t *name, uint8_t name_len,
                                     int rssi) {
  if (find_scan_result(bda, ble_scan_results_)) {
    ESP_LOGW(TAG, "Result already exists!");
    return;
  }

  auto r = std::make_unique<esp_hid_scan_result_t>();

  if (r == nullptr) {
    ESP_LOGE(TAG, "make_unique of ble_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BLE;

  memcpy(r->bda, bda, sizeof(esp_bd_addr_t));

  r->ble.appearance = appearance;
  r->ble.addr_type  = addr_type;
  r->usage          = esp_hid_usage_from_appearance(appearance);
  r->rssi           = rssi;
  r->name.clear();

  if (name_len && name) {
    r->name.assign(reinterpret_cast<const char *>(name), name_len);
  }

  ble_scan_results_.push_front(std::move(r));
  num_ble_scan_results_++;
}

/**
 * @brief Handles Bluetooth device discovery results.
 *
 * This method processes the callback parameters received during Bluetooth device discovery.
 * It extracts and logs various device properties including:
 * - Device address
 * - Device name
 * - RSSI (signal strength)
 * - Class of Device (COD) information including major class, minor class, and services
 * - UUID information (16-bit, 32-bit, and 128-bit UUIDs)
 *
 * The method also attempts to find device names in EIR (Extended Inquiry Response) data
 * if not found in the primary device properties.
 *
 * After processing, if the device is a peripheral or already exists in scan results,
 * it is added/updated in the scan results list.
 *
 * @param param Pointer to the ESP Bluetooth GAP callback parameters containing
 *              the discovery result information
 */
void BTKeyboard::handle_bt_device_result(esp_bt_gap_cb_param_t *param) {
  std::cout << "BT: " << param->disc_res.bda;

  uint32_t      codv     = 0;
  esp_bt_cod_t *cod      = (esp_bt_cod_t *)&codv;
  int8_t        rssi     = 0;
  uint8_t      *name     = nullptr;
  uint8_t       name_len = 0;
  esp_bt_uuid_t uuid;

  uuid.len         = ESP_UUID_LEN_16;
  uuid.uuid.uuid16 = 0;

  for (int i = 0; i < param->disc_res.num_prop; i++) {
    esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
    if (prop->type != ESP_BT_GAP_DEV_PROP_EIR) {
      std::cout << ", " << gap_bt_prop_type_names_[prop->type] << ": ";
    }
    if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
      name     = (uint8_t *)prop->val;
      name_len = strlen((const char *)name);
      std::cout << name;
    } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
      rssi = *((int8_t *)prop->val);
      std::cout << std::dec << +rssi;
    } else if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
      memcpy(&codv, prop->val, sizeof(uint32_t));
      std::cout << "major: " << esp_hid_cod_major_str(cod->major) << ", minor: " << cod->minor
                << ", service: 0x" << std::hex << std::setw(3) << std::setfill('0') << cod->service;
    } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
      uint8_t  len  = 0;
      uint8_t *data = 0;

      data =
          esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_16BITS_UUID, &len);

      if (data == nullptr) {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_INCMPL_16BITS_UUID,
                                           &len);
      }

      if (data && len == ESP_UUID_LEN_16) {
        uuid.len         = ESP_UUID_LEN_16;
        uuid.uuid.uuid16 = data[0] + (data[1] << 8);
        std::cout << ", " << uuid;
        continue;
      }

      data =
          esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_32BITS_UUID, &len);

      if (data == nullptr) {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_INCMPL_32BITS_UUID,
                                           &len);
      }

      if (data && len == ESP_UUID_LEN_32) {
        uuid.len = len;
        memcpy(&uuid.uuid.uuid32, data, sizeof(uint32_t));
        std::cout << ", " << uuid;
        continue;
      }

      data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_128BITS_UUID,
                                         &len);

      if (data == nullptr) {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val,
                                           ESP_BT_EIR_TYPE_INCMPL_128BITS_UUID, &len);
      }

      if (data && len == ESP_UUID_LEN_128) {
        uuid.len = len;
        memcpy(uuid.uuid.uuid128, (uint8_t *)data, len);
        std::cout << ", " << uuid;
        continue;
      }

      // try to find a name
      if (name == nullptr) {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,
                                           &len);

        if (data == nullptr) {
          data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,
                                             &len);
        }

        if (data && len) {
          name     = data;
          name_len = len;
          std::cout << ", NAME: ";
          for (int x = 0; x < len; x++) {
            std::cout << (char)data[x];
          }
        }
      }
    }
  }
  std::cout << std::dec << std::endl;

  if ((cod->major == ESP_BT_COD_MAJOR_DEV_PERIPHERAL) ||
      (find_scan_result(param->disc_res.bda, bt_scan_results_) != nullptr)) {
    add_bt_scan_result(param->disc_res.bda, cod, &uuid, name, name_len, rssi);
  }
}

/**
 * @brief Bluetooth GAP event handler callback function
 *
 * Handles various Bluetooth GAP (Generic Access Profile) events including:
 * - Discovery state changes
 * - Device discovery results
 * - Pairing/Authentication events (PIN requests, passkey notifications)
 * - Mode changes
 *
 * @param event The GAP event type that occurred
 * @param param Pointer to structure containing event-specific parameters
 *
 * Events handled:
 * - ESP_BT_GAP_DISC_STATE_CHANGED_EVT: Discovery started/stopped
 * - ESP_BT_GAP_DISC_RES_EVT: Device discovery result
 * - ESP_BT_GAP_KEY_NOTIF_EVT: Passkey notification
 * - ESP_BT_GAP_CFM_REQ_EVT: Confirmation request
 * - ESP_BT_GAP_KEY_REQ_EVT: Passkey request
 * - ESP_BT_GAP_MODE_CHG_EVT: Mode change
 * - ESP_BT_GAP_PIN_REQ_EVT: PIN code request
 */
void BTKeyboard::bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      {
        ESP_LOGD(TAG, "BT GAP DISC_STATE %s",
                 (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) ? "START" : "STOP");
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
          SEND_BT_CB();
        }
        break;
      }
    case ESP_BT_GAP_DISC_RES_EVT:
      {
        bt_keyboard_->handle_bt_device_result(param);
        break;
      }
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      ESP_LOGD(TAG, "BT GAP KEY_NOTIF passkey: %ld", param->key_notif.passkey);
      if (pairing_handler_ != nullptr) (*pairing_handler_)(param->key_notif.passkey);
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      {
        ESP_LOGD(TAG, "BT GAP CFM_REQ_EVT Please compare the numeric value: %" PRIu32,
                 param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
      }
    case ESP_BT_GAP_KEY_REQ_EVT:
      ESP_LOGD(TAG, "BT GAP KEY_REQ_EVT Please enter passkey!");
      break;
    case ESP_BT_GAP_MODE_CHG_EVT:
      ESP_LOGD(TAG, "BT GAP MODE_CHG_EVT mode:%d", param->mode_chg.mode);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT:
      {
        ESP_LOGD(TAG, "BT GAP PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
          ESP_LOGD(TAG, "Input pin code: 0000 0000 0000 0000");
          esp_bt_pin_code_t pin_code = {0};
          esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
          ESP_LOGD(TAG, "Input pin code: 1234");
          esp_bt_pin_code_t pin_code;
          pin_code[0] = '1';
          pin_code[1] = '2';
          pin_code[2] = '3';
          pin_code[3] = '4';
          esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
      }
    default:
      ESP_LOGD(TAG, "BT GAP EVENT %s", bt_gap_evt_str(event));
      break;
  }
}

void BTKeyboard::handle_ble_device_result(esp_ble_gap_cb_param_t *param) {
  uint16_t uuid       = 0;
  uint16_t appearance = 0;
  char     name[64]   = {0};

  auto &scan_rst      = param->scan_rst;

  uint8_t  uuid_len   = 0;
  uint8_t *uuid_d     = esp_ble_resolve_adv_data_by_type(scan_rst.ble_adv,
                                                         scan_rst.adv_data_len + scan_rst.scan_rsp_len,
                                                         ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid_len);

  if (uuid_d != nullptr && uuid_len) {
    uuid = uuid_d[0] + (uuid_d[1] << 8);
  }

  uint8_t  appearance_len = 0;
  uint8_t *appearance_d   = esp_ble_resolve_adv_data_by_type(
      scan_rst.ble_adv, scan_rst.adv_data_len + scan_rst.scan_rsp_len, ESP_BLE_AD_TYPE_APPEARANCE,
      &appearance_len);

  if (appearance_d != nullptr && appearance_len) {
    appearance = appearance_d[0] + (appearance_d[1] << 8);
  }

  uint8_t  adv_name_len = 0;
  uint8_t *adv_name     = esp_ble_resolve_adv_data_by_type(
      scan_rst.ble_adv, scan_rst.adv_data_len + scan_rst.scan_rsp_len, ESP_BLE_AD_TYPE_NAME_CMPL,
      &adv_name_len);

  if (adv_name == nullptr) {
    adv_name = esp_ble_resolve_adv_data_by_type(scan_rst.ble_adv,
                                                scan_rst.adv_data_len + scan_rst.scan_rsp_len,
                                                ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
  }

  if (adv_name != nullptr && adv_name_len) {
    memcpy(name, adv_name, adv_name_len);
    name[adv_name_len] = 0;
  }

  std::cout << "BLE: " << scan_rst.bda << ", " << "RSSI: " << std::dec << scan_rst.rssi << ", "
            << "UUID: 0x" << std::hex << std::setw(4) << std::setfill('0') << uuid << ", "
            << "APPEARANCE: 0x" << std::hex << std::setw(4) << std::setfill('0') << appearance
            << ", " << "ADDR_TYPE: " << ble_addr_type_str(scan_rst.ble_addr_type);

  if (adv_name_len) {
    std::cout << ", NAME: '" << name << "'";
  }
  std::cout << std::dec << std::endl;

  if (uuid == ESP_GATT_UUID_HID_SVC) {
    add_ble_scan_result(scan_rst.bda, scan_rst.ble_addr_type, appearance, adv_name, adv_name_len,
                        scan_rst.rssi);
  }
}

/**
 * @brief Handles BLE GAP (Generic Access Profile) events
 *
 * This callback function processes various BLE GAP events including:
 * - Scanning related events (param set, results, stop)
 * - Advertisement events (data set, start)
 * - Authentication events (completion, key exchange, passkey handling)
 *
 * For authentication, it handles different IO capability scenarios:
 * - ESP_IO_CAP_OUT: Displays passkey to user
 * - ESP_IO_CAP_IO: Handles numeric comparison
 * - ESP_IO_CAP_IN: Handles passkey input
 *
 * @param event The GAP event type being processed
 * @param param Parameters associated with the event
 */
void BTKeyboard::ble_gap_event_handler(esp_gap_ble_cb_event_t  event,
                                       esp_ble_gap_cb_param_t *param) {
  switch (event) {

      // SCAN

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      {
        ESP_LOGD(TAG, "BLE GAP EVENT SCAN_PARAM_SET_COMPLETE");
        SEND_BLE_CB();
        break;
      }
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
      {
        switch (param->scan_rst.search_evt) {
          case ESP_GAP_SEARCH_INQ_RES_EVT:
            {
              bt_keyboard_->handle_ble_device_result(param);
              break;
            }
          case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGD(TAG, "BLE GAP EVENT SCAN DONE: %d", param->scan_rst.num_resps);
            SEND_BLE_CB();
            break;
          default:
            break;
        }
        break;
      }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      {
        ESP_LOGD(TAG, "BLE GAP EVENT SCAN CANCELED");
        break;
      }

      // ADVERTISEMENT

    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
      ESP_LOGD(TAG, "BLE GAP ADV_DATA_SET_COMPLETE");
      break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      ESP_LOGD(TAG, "BLE GAP ADV_START_COMPLETE");
      break;

      // AUTHENTICATION

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      if (!param->ble_security.auth_cmpl.success) {
        ESP_LOGE(TAG, "BLE GAP AUTH ERROR: 0x%x", param->ble_security.auth_cmpl.fail_reason);
      } else {
        ESP_LOGD(TAG, "BLE GAP AUTH SUCCESS");
      }
      break;

    case ESP_GAP_BLE_KEY_EVT: // shows the ble key info share with peer device to the user.
      ESP_LOGD(TAG, "BLE GAP KEY type = %s",
               ble_key_type_str(param->ble_security.ble_key.key_type));
      break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: // ESP_IO_CAP_OUT
      // The app will receive this evt when the IO has Output capability and the peer device IO has
      // Input capability. Show the passkey number to the user to input it in the peer device.
      ESP_LOGD(TAG, "BLE GAP PASSKEY_NOTIF passkey:%ld", param->ble_security.key_notif.passkey);
      if (pairing_handler_ != nullptr) (*pairing_handler_)(param->ble_security.key_notif.passkey);
      break;

    case ESP_GAP_BLE_NC_REQ_EVT: // ESP_IO_CAP_IO
      // The app will receive this event when the IO has DisplayYesNO capability and the peer device
      // IO also has DisplayYesNo capability. show the passkey number to the user to confirm it with
      // the number displayed by peer device.
      ESP_LOGD(TAG, "BLE GAP NC_REQ passkey:%ld", param->ble_security.key_notif.passkey);
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
      break;

    case ESP_GAP_BLE_PASSKEY_REQ_EVT: // ESP_IO_CAP_IN
      // The app will receive this evt when the IO has Input capability and the peer device IO has
      // Output capability. See the passkey number on the peer device and send it back.
      ESP_LOGD(TAG, "BLE GAP PASSKEY_REQ");
      // esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, 1234);
      break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
      ESP_LOGD(TAG, "BLE GAP SEC_REQ");
      // Send the positive(true) security response to the peer device to accept the security
      // request. If not accept the security request, should send the security response with
      // negative(false) accept value.
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      break;

    default:
      ESP_LOGD(TAG, "BLE GAP EVENT %s", ble_gap_evt_str(event));
      break;
  }
}

/**
 * @brief Start Bluetooth device discovery/scanning
 *
 * Initiates a Bluetooth scan for nearby devices using general inquiry mode.
 * The scan duration is calculated by dividing the seconds parameter by 1.28
 * to convert to Bluetooth time units.
 *
 * @param seconds The duration of the scan in seconds
 * @return esp_err_t ESP_OK on success, or an error code if starting discovery fails
 */
esp_err_t BTKeyboard::start_bt_scan(uint32_t seconds) {
  esp_err_t ret = ESP_OK;
  if ((ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (int)(seconds / 1.28),
                                        0)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_bt_gap_start_discovery failed: %d", ret);
    return ret;
  }
  return ret;
}

static esp_ble_scan_params_t hid_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE,
};

/**
 * @brief Start BLE scanning for HID devices
 *
 * This function initiates Bluetooth Low Energy scanning using the pre-configured HID scan
 * parameters. It first sets the scan parameters and then starts the actual scanning process.
 *
 * @param seconds Duration of the scan in seconds. If set to 0, scanning continues indefinitely
 * @return esp_err_t ESP_OK on success, or an error code if setting parameters or starting scan
 * fails
 */
esp_err_t BTKeyboard::start_ble_scan(uint32_t seconds) {

  esp_err_t ret = ESP_OK;
  if ((ret = esp_ble_gap_set_scan_params(&hid_scan_params)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_set_scan_params failed: %d", ret);
    return ret;
  }
  WAIT_BLE_CB();

  if ((ret = esp_ble_gap_start_scanning(seconds)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_start_scanning failed: %d", ret);
    return ret;
  }
  return ret;
}

/**
 * @brief Performs a scan for both Bluetooth Classic and BLE HID devices
 *
 * This method initiates concurrent scans for both Bluetooth Classic and BLE HID devices.
 * It first checks if there are any pending scan results that need to be cleared.
 * Then it initiates both BLE and BT Classic scans and waits for their completion.
 * Finally, it combines the results from both scans into a single result list.
 *
 * @param seconds Duration of the scan in seconds
 * @param num_results Pointer to store the total number of devices found
 * @param results Reference to a ScanResult container to store the found devices
 *
 * @return ESP_OK if scan completed successfully
 *         ESP_FAIL if there are pending results or if either scan fails to start
 *
 * @note The method will clear any previous scan results before starting a new scan
 * @note Both BT Classic and BLE scan results are combined in the results container
 */
esp_err_t BTKeyboard::esp_hid_scan(uint32_t seconds, size_t *num_results, ScanResult &results) {
  if (num_bt_scan_results_ || !bt_scan_results_.empty() || num_ble_scan_results_ ||
      !ble_scan_results_.empty()) {
    ESP_LOGE(TAG, "There are old scan results. Free them first!");
    return ESP_FAIL;
  }

  if (start_ble_scan(seconds) == ESP_OK) {
    WAIT_BLE_CB();
  } else {
    return ESP_FAIL;
  }

  if (start_bt_scan(seconds) == ESP_OK) {
    WAIT_BT_CB();
  } else {
    return ESP_FAIL;
  }

  *num_results = num_bt_scan_results_ + num_ble_scan_results_;

  for (auto &r : bt_scan_results_) {
    results.push_front(std::move(r));
  }
  for (auto &r : ble_scan_results_) {
    results.push_front(std::move(r));
  }

  num_bt_scan_results_ = 0;
  bt_scan_results_.clear();

  num_ble_scan_results_ = 0;
  ble_scan_results_.clear();

  return ESP_OK;
}

/**
 * @brief Retrieves the list of bonded Bluetooth devices
 *
 * This method queries the ESP32 Bluetooth stack for all devices that have been
 * previously bonded with this device. It returns both the list of devices and
 * their count.
 *
 * @return A pair containing:
 *         - A shared pointer to an array of esp_ble_bond_dev_t structures containing
 *           the bonded devices information. nullptr if no devices or on error.
 *         - An integer representing the number of bonded devices. 0 if no devices
 *           or on error.
 *
 * @note The caller does not need to free the returned pointer as it is managed by
 *       the shared_ptr.
 */
auto BTKeyboard::retrieve_bonded_devices()
    -> std::pair<std::shared_ptr<esp_ble_bond_dev_t[]>, int> {
  int bonded_devices_count = esp_ble_get_bond_device_num();
  ESP_LOGD(TAG, "Number of bonded devices: %d", bonded_devices_count);

  if (bonded_devices_count == 0) {
    ESP_LOGD(TAG, "No bonded devices");
    return {nullptr, 0};
  }

  auto bonded_devices = std::make_shared<esp_ble_bond_dev_t[]>(bonded_devices_count);

  if ((esp_ble_get_bond_device_list((int *)&bonded_devices_count, bonded_devices.get())) !=
      ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_get_bond_device_list failed");
    return {nullptr, 0};
  }

  return {bonded_devices, bonded_devices_count};
}

/**
 * @brief Scan for HID devices and attempt to connect to the first keyboard found
 *
 * This method scans for both Bluetooth Classic and BLE HID devices. For each device found,
 * it displays detailed information including:
 * - Transport type (BLE or BT Classic)
 * - Device address
 * - RSSI signal strength
 * - HID usage type
 * - For BLE: appearance value and address type
 * - For BT Classic: Class of Device (COD) information
 * - Device name (if available)
 *
 * The scan will automatically connect to the first device that matches either:
 * - For BLE: Has an appearance value matching ESP_BLE_APPEARANCE_HID_KEYBOARD
 * - For BT Classic: Has major class PERIPHERAL (5) and minor class includes keyboard
 *
 * @param seconds_wait_time Duration of the scan in seconds
 *
 * @note The method will return immediately if the keyboard is already connected
 */
void BTKeyboard::devices_scan(int seconds_wait_time) {

  if (connected_) return;

  size_t     results_len = 0;
  ScanResult results;
  ESP_LOGD(TAG, "SCAN...");

  // start scan for HID devices

  esp_hid_scan(seconds_wait_time, &results_len, results);
  ESP_LOGD(TAG, "SCAN: %u results", results_len);

  if (results_len) {
    esp_hid_scan_result_t *cr = nullptr;
    for (auto &r : results) {
      uint16_t appearance = r->ble.appearance;
      std::cout << "  " << (r->transport == ESP_HID_TRANSPORT_BLE ? "BLE: " : "BT: ") << r->bda
                << std::dec << ", RSSI: " << +r->rssi << ", USAGE: " << esp_hid_usage_str(r->usage);
      if (r->transport == ESP_HID_TRANSPORT_BLE) {
        std::cout << ", APPEARANCE: 0x" << std::hex << std::setw(4) << std::setfill('0')
                  << appearance << ", ADDR_TYPE: '" << ble_addr_type_str(r->ble.addr_type) << "'";
        if (appearance == ESP_BLE_APPEARANCE_HID_KEYBOARD) {
          cr = r.get();
        }
      }
      if (r->transport == ESP_HID_TRANSPORT_BT) {
        std::cout << ", COD: " << esp_hid_cod_major_str(r->bt.cod.major) << "[";
        esp_hid_cod_minor_print(r->bt.cod.minor, stdout);
        std::cout << "] srv 0x" << std::hex << std::setw(3) << std::setfill('0')
                  << r->bt.cod.service << ", " << r->bt.uuid;

        if ((r->bt.cod.major == 5 /* PERIPHERAL */) &&
            (r->bt.cod.minor & ESP_HID_COD_MIN_KEYBOARD)) {
          cr = r.get();
        }
      }

      std::cout << std::dec;

      if (!r->name.empty()) {
        std::cout << ", NAME: " << r->name << std::endl;
      } else {
        std::cout << std::endl;
      }

      if (cr) break;
    }

    if (cr) {
      // open the selected entry
      esp_hidh_dev_open(cr->bda, cr->transport, cr->ble.addr_type);
    }

    // free the results
    results.clear();
  }
}

/**
 * @brief Bluetooth HID Host callback function to handle various HID events
 *
 * This callback handles the following events:
 * - ESP_HIDH_OPEN_EVENT: Device connection opened
 * - ESP_HIDH_BATTERY_EVENT: Battery level updates
 * - ESP_HIDH_INPUT_EVENT: Input reports from device
 * - ESP_HIDH_FEATURE_EVENT: Feature reports from device
 * - ESP_HIDH_CLOSE_EVENT: Device connection closed
 *
 * For each event, it logs relevant information and updates the BTKeyboard state accordingly.
 *
 * @param handler_args Pointer to handler-specific arguments
 * @param base Event base (unused)
 * @param id Event ID (esp_hidh_event_t)
 * @param event_data Pointer to event-specific data
 */
void BTKeyboard::hidh_callback(void *handler_args, esp_event_base_t base, int32_t id,
                               void *event_data) {
  esp_hidh_event_t       event = (esp_hidh_event_t)id;
  esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

  switch (event) {
    case ESP_HIDH_OPEN_EVENT:
      {
        if (param->open.status == ESP_OK) {
          const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
          if (bda) {
            ESP_LOGD(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda),
                     esp_hidh_dev_name_get(param->open.dev));
            esp_hidh_dev_dump(param->open.dev, stdout);
            bt_keyboard_->set_connected(true);
          }
        } else {
          ESP_LOGE(TAG, " OPEN failed!");
          bt_keyboard_->set_connected(false);
        }
        break;
      }
    case ESP_HIDH_BATTERY_EVENT:
      {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
        if (bda) {
          ESP_LOGD(TAG, ESP_BD_ADDR_STR " BATTERY: %d%%", ESP_BD_ADDR_HEX(bda),
                   param->battery.level);
          bt_keyboard_->set_battery_level(param->battery.level);
        }
        break;
      }
    case ESP_HIDH_INPUT_EVENT:
      {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->input.dev);
        if (bda) {
          ESP_LOGD(TAG, ESP_BD_ADDR_STR " INPUT: %8s, MAP: %2u, ID: %3u, Len: %d, Data:",
                   ESP_BD_ADDR_HEX(bda), esp_hid_usage_str(param->input.usage),
                   param->input.map_index, param->input.report_id, param->input.length);
          ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->input.data, param->input.length, ESP_LOG_DEBUG);
          bt_keyboard_->push_key(param->input.data, param->input.length);
        }
        break;
      }
    case ESP_HIDH_FEATURE_EVENT:
      {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
        if (bda) {
          ESP_LOGD(TAG, ESP_BD_ADDR_STR " FEATURE: %8s, MAP: %2u, ID: %3u, Len: %d",
                   ESP_BD_ADDR_HEX(bda), esp_hid_usage_str(param->feature.usage),
                   param->feature.map_index, param->feature.report_id, param->feature.length);
          ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->feature.data, param->feature.length, ESP_LOG_DEBUG);
        }
        break;
      }
    case ESP_HIDH_CLOSE_EVENT:
      {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
        if (bda) {
          ESP_LOGD(TAG, ESP_BD_ADDR_STR " CLOSE: %s", ESP_BD_ADDR_HEX(bda),
                   esp_hidh_dev_name_get(param->close.dev));
          bt_keyboard_->set_connected(false);
        }
        break;
      }
    default:
      ESP_LOGD(TAG, "EVENT: %d", event);
      break;
  }
}

/**
 * @brief Pushes keyboard event data to the event queue
 *
 * This method takes raw keyboard event data and enqueues it for processing.
 * If the input size exceeds MAX_KEY_DATA_SIZE, it will be truncated and a
 * warning message will be logged.
 *
 * @param keys Pointer to array containing keyboard event data
 * @param size Size of the keyboard event data in bytes
 *
 * @note The data is copied into a KeyInfo struct before being enqueued
 * @note No blocking occurs if queue is full (timeout = 0)
 */
void BTKeyboard::push_key(uint8_t *keys, uint8_t size) {
  KeyInfo inf;
  if (size > MAX_KEY_DATA_SIZE) {
    ESP_LOGW(TAG, "Keyboard event data size bigger than expected: %d\n.", size);
    size = MAX_KEY_DATA_SIZE;
  }

  // enqueue
  inf.size = size;
  memcpy(&inf.keys, keys, size);

  xQueueSendToBack(event_queue_, &inf, 0);
}

/**
 * @brief Waits for and processes keyboard input to return an ASCII character.
 *
 * This method handles keyboard input processing including:
 * - Key repeat functionality
 * - Modifier keys (Shift, Ctrl)
 * - Caps Lock toggle
 * - Character translation using shift translation dictionary
 *
 * @param forever If true, waits indefinitely for input. If false, returns immediately if no input
 *                is available and last character was 0, otherwise waits for repeat period.
 *
 * @return ASCII character based on the keyboard input and current modifier state.
 *         Returns:
 *         - Control characters (1-26) when Ctrl is pressed with letters
 *         - Shifted or unshifted characters based on Shift and Caps Lock states
 *         - Last character on key repeat
 *         - 0 if no valid character could be generated
 *
 * @note The method manages internal repeat timing and caps lock state.
 */
char BTKeyboard::wait_for_ascii_char(bool forever) {
  KeyInfo inf;

  while (true) {
    if (!wait_for_low_event(inf,
                            (last_ch_ == 0) ? (forever ? portMAX_DELAY : 0) : repeat_period_)) {
      repeat_period_ = pdMS_TO_TICKS(120);
      return last_ch_;
    }

    int k = -1;
    for (int i = 0; i < MAX_KEY_DATA_SIZE; i++) {
      if ((k < 0) && key_avail_[i]) k = i;
      key_avail_[i] = inf.keys[i] == 0;
    }

    if (k < 0) {
      continue;
    }

    char ch = inf.keys[k];

    if (ch >= 4) {
      if ((uint8_t)inf.modifier & CTRL_MASK) {
        if (ch < (3 + 26)) {
          repeat_period_  = pdMS_TO_TICKS(500);
          return last_ch_ = (ch - 3);
        }
      } else if (ch <= 0x52) {
        // ESP_LOGD(TAG, "Scan code: %d", ch);
        if (ch == KEY_CAPS_LOCK) caps_lock_ = !caps_lock_;
        if ((uint8_t)inf.modifier & SHIFT_MASK) {
          if (caps_lock_) {
            repeat_period_  = pdMS_TO_TICKS(500);
            return last_ch_ = shift_trans_dict_[(ch - 4) << 1];
          } else {
            repeat_period_  = pdMS_TO_TICKS(500);
            return last_ch_ = shift_trans_dict_[((ch - 4) << 1) + 1];
          }
        } else {
          if (caps_lock_) {
            repeat_period_  = pdMS_TO_TICKS(500);
            return last_ch_ = shift_trans_dict_[((ch - 4) << 1) + 1];
          } else {
            repeat_period_  = pdMS_TO_TICKS(500);
            return last_ch_ = shift_trans_dict_[(ch - 4) << 1];
          }
        }
      }
    }

    last_ch_ = 0;
  }
}

/**
 * @brief Display information about all Bluetooth devices currently bonded with this keyboard
 *
 * This method retrieves and prints the list of all bonded Bluetooth devices to the log.
 * For each device, it shows:
 * - Device index
 * - Address type
 * - Device MAC address
 *
 * If no devices are bonded, it logs a message indicating this.
 *
 * The logging is done using ESP-IDF's logging facility at INFO level.
 */
void BTKeyboard::show_bonded_devices(void) {
  auto [dev_list, dev_count] = retrieve_bonded_devices();

  if (dev_count == 0) {
    ESP_LOGD(TAG, "There is no bonded device.\n");
    return;
  }

  ESP_LOGD(TAG, "Bonded devices count %d.", dev_count);
  for (int i = 0; i < dev_count; i++) {
    ESP_LOGD(TAG, "[%u] addr_type %u, addr " ESP_BD_ADDR_STR, i, dev_list[i].bd_addr_type,
             ESP_BD_ADDR_HEX(dev_list[i].bd_addr));
  }
}

/**
 * @brief Removes all bonded Bluetooth devices from the device's memory
 *
 * This method retrieves the list of currently bonded devices and removes them
 * one by one. If no devices are bonded, a log message is displayed and the
 * method returns without performing any removal.
 *
 * The removal process uses the ESP32's Bluetooth API to remove each device's
 * bonding information from the persistent storage.
 */
void BTKeyboard::remove_all_bonded_devices() {

  auto [dev_list, dev_count] = retrieve_bonded_devices();

  if (dev_count == 0) {
    ESP_LOGD(TAG, "There is no bonded device.");
    return;
  }

  for (int i = 0; i < dev_count; i++) {
    esp_ble_remove_bond_device(dev_list[i].bd_addr);
  }
}
