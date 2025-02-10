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
#include <ostream>

#define SCAN 1

#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof(*a))

#define WAIT_BT_CB() xSemaphoreTake(bt_hidh_cb_semaphore_, portMAX_DELAY)
#define SEND_BT_CB() xSemaphoreGive(bt_hidh_cb_semaphore_)

#define WAIT_BLE_CB() xSemaphoreTake(ble_hidh_cb_semaphore_, portMAX_DELAY)
#define SEND_BLE_CB() xSemaphoreGive(ble_hidh_cb_semaphore_)

SemaphoreHandle_t BTKeyboard::bt_hidh_cb_semaphore_  = nullptr;
SemaphoreHandle_t BTKeyboard::ble_hidh_cb_semaphore_ = nullptr;

const char *BTKeyboard::gap_bt_prop_type_names_[] = {"", "BDNAME", "COD", "RSSI", "EIR"};
const char *BTKeyboard::ble_gap_evt_names_[]      = {"ADV_DATA_SET_COMPLETE",
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
const char *BTKeyboard::bt_gap_evt_names_[]       = {
    "DISC_RES", "DISC_STATE_CHANGED", "RMT_SRVCS", "RMT_SRVC_REC",   "AUTH_CMPL", "PIN_REQ",
    "CFM_REQ",  "KEY_NOTIF",          "KEY_REQ",   "READ_RSSI_DELTA"};
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

BTKeyboard *BTKeyboard::bt_keyboard_                                    = nullptr;
BTKeyboard::PairingHandler *BTKeyboard::pairing_handler_                = nullptr;
BTKeyboard::GotConnectionHandler *BTKeyboard::got_connection_handler_   = nullptr;
BTKeyboard::LostConnectionHandler *BTKeyboard::lost_connection_handler_ = nullptr;

bool BTKeyboard::connected_ = false;

static std::ostream &operator<<(std::ostream &os, const esp_bd_addr_t &addr) {
  std::ios_base::fmtflags oldFlags = os.flags();
  char oldFill                     = os.fill('0');
  char oldWidth                    = os.width();

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

static std::ostream &operator<<(std::ostream &os, const esp_bt_uuid_t &uuid) {
  std::ios_base::fmtflags oldFlags = os.flags();
  char oldFill                     = os.fill('0');
  char oldWidth                    = os.width();

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

const char *BTKeyboard::ble_addr_type_str(esp_ble_addr_type_t ble_addr_type) {
  if (ble_addr_type > BLE_ADDR_TYPE_RPA_RANDOM) {
    return "UNKNOWN";
  }
  return ble_addr_type_names_[ble_addr_type];
}

const char *BTKeyboard::ble_gap_evt_str(uint8_t event) {
  if (event >= SIZEOF_ARRAY(ble_gap_evt_names_)) {
    return "UNKNOWN";
  }
  return ble_gap_evt_names_[event];
}

const char *BTKeyboard::bt_gap_evt_str(uint8_t event) {
  if (event >= SIZEOF_ARRAY(bt_gap_evt_names_)) {
    return "UNKNOWN";
  }
  return bt_gap_evt_names_[event];
}

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

void BTKeyboard::esp_hid_scan_results_free(esp_hid_scan_result_t *results) {
  esp_hid_scan_result_t *r = nullptr;
  while (results) {
    r       = results;
    results = results->next;
    if (r->name != nullptr) {
      free((char *)r->name);
    }
    free(r);
  }
}

BTKeyboard::esp_hid_scan_result_t *BTKeyboard::find_scan_result(esp_bd_addr_t bda,
                                                                esp_hid_scan_result_t *results) {
  esp_hid_scan_result_t *r = results;
  while (r) {
    if (memcmp(bda, r->bda, sizeof(esp_bd_addr_t)) == 0) {
      return r;
    }
    r = r->next;
  }
  return nullptr;
}

void BTKeyboard::add_bt_scan_result(esp_bd_addr_t bda, esp_bt_cod_t *cod, esp_bt_uuid_t *uuid,
                                    uint8_t *name, uint8_t name_len, int rssi) {
  esp_hid_scan_result_t *r = find_scan_result(bda, bt_scan_results_);
  if (r) {
    // Some info may come later
    if (r->name == nullptr && name && name_len) {
      char *name_s = (char *)malloc(name_len + 1);
      if (name_s == nullptr) {
        ESP_LOGE(TAG, "Malloc result name failed!");
        return;
      }
      memcpy(name_s, name, name_len);
      name_s[name_len] = 0;
      r->name          = (const char *)name_s;
    }
    if (r->bt.uuid.len == 0 && uuid->len) {
      memcpy(&r->bt.uuid, uuid, sizeof(esp_bt_uuid_t));
    }
    if (rssi != 0) {
      r->rssi = rssi;
    }
    return;
  }

  r = (esp_hid_scan_result_t *)malloc(sizeof(esp_hid_scan_result_t));

  if (r == nullptr) {
    ESP_LOGE(TAG, "Malloc bt_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BT;

  memcpy(r->bda, bda, sizeof(esp_bd_addr_t));
  memcpy(&r->bt.cod, cod, sizeof(esp_bt_cod_t));
  memcpy(&r->bt.uuid, uuid, sizeof(esp_bt_uuid_t));

  r->usage = esp_hid_usage_from_cod((uint32_t)cod);
  r->rssi  = rssi;
  r->name  = nullptr;

  if (name_len && name) {
    char *name_s = (char *)malloc(name_len + 1);
    if (name_s == nullptr) {
      free(r);
      ESP_LOGE(TAG, "Malloc result name failed!");
      return;
    }
    memcpy(name_s, name, name_len);
    name_s[name_len] = 0;
    r->name          = (const char *)name_s;
  }
  r->next          = bt_scan_results_;
  bt_scan_results_ = r;
  num_bt_scan_results_++;
}

void BTKeyboard::add_ble_scan_result(esp_bd_addr_t bda, esp_ble_addr_type_t addr_type,
                                     uint16_t appearance, uint8_t *name, uint8_t name_len,
                                     int rssi) {
  if (find_scan_result(bda, ble_scan_results_)) {
    ESP_LOGW(TAG, "Result already exists!");
    return;
  }

  esp_hid_scan_result_t *r = (esp_hid_scan_result_t *)malloc(sizeof(esp_hid_scan_result_t));

  if (r == nullptr) {
    ESP_LOGE(TAG, "Malloc ble_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BLE;

  memcpy(r->bda, bda, sizeof(esp_bd_addr_t));

  r->ble.appearance = appearance;
  r->ble.addr_type  = addr_type;
  r->usage          = esp_hid_usage_from_appearance(appearance);
  r->rssi           = rssi;
  r->name           = nullptr;

  if (name_len && name) {
    char *name_s = (char *)malloc(name_len + 1);
    if (name_s == nullptr) {
      free(r);
      ESP_LOGE(TAG, "Malloc result name failed!");
      return;
    }
    memcpy(name_s, name, name_len);
    name_s[name_len] = 0;
    r->name          = (const char *)name_s;
  }

  r->next           = ble_scan_results_;
  ble_scan_results_ = r;
  num_ble_scan_results_++;
}

bool BTKeyboard::setup(PairingHandler *pairing_handler,
                       GotConnectionHandler *got_connection_handler,
                       LostConnectionHandler *lost_connection_handler) {

  esp_err_t ret;
  const esp_bt_mode_t mode = HID_HOST_MODE;

  if (bt_keyboard_ != nullptr) {
    ESP_LOGE(TAG, "Setup called more than once. Only one instance of BTKeyboard is allowed.");
    return false;
  }

  bt_keyboard_ = this;

  pairing_handler_         = pairing_handler;
  lost_connection_handler_ = lost_connection_handler;

  event_queue_ = xQueueCreate(10, sizeof(KeyInfo));

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

  bt_cfg.mode             = mode;
  bt_cfg.bt_max_acl_conn  = 3;
  bt_cfg.bt_max_sync_conn = 3;

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
  esp_bt_io_cap_t iocap        = ESP_BT_IO_CAP_IO;
  esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

  /*
   * Set default parameters for Legacy Pairing
   * Use fixed pin code
   */
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  pin_code[0] = '1';
  pin_code[1] = '2';
  pin_code[2] = '3';
  pin_code[3] = '4';
  pin_code[4] = '5';
  pin_code[5] = '6';
  pin_code[6] = '7';
  pin_code[7] = '8';
  pin_code[8] = 0;
  esp_bt_gap_set_pin(pin_type, 8, pin_code);

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
      .callback         = hidh_callback,
      .event_stack_size = 4 * 1024, // Required with ESP-IDF 4.4
      .callback_arg     = nullptr   // idem
  };
  ESP_ERROR_CHECK(esp_hidh_init(&config));

  for (int i = 0; i < MAX_KEY_DATA_SIZE; i++) {
    key_avail_[i] = true;
  }

  last_ch_       = 0;
  battery_level_ = -1;
  return true;
}

void BTKeyboard::handle_bt_device_result(esp_bt_gap_cb_param_t *param) {
  std::cout << "BT: " << param->disc_res.bda;

  uint32_t codv     = 0;
  esp_bt_cod_t *cod = (esp_bt_cod_t *)&codv;
  int8_t rssi       = 0;
  uint8_t *name     = nullptr;
  uint8_t name_len  = 0;
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
      uint8_t len   = 0;
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
  std::cout << std::endl;

  if ((cod->major == ESP_BT_COD_MAJOR_DEV_PERIPHERAL) ||
      (find_scan_result(param->disc_res.bda, bt_scan_results_) != nullptr)) {
    add_bt_scan_result(param->disc_res.bda, cod, &uuid, name, name_len, rssi);
  }
}

void BTKeyboard::bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
  case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
    ESP_LOGV(TAG, "BT GAP DISC_STATE %s",
             (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) ? "START" : "STOP");
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      SEND_BT_CB();
    }
    break;
  }
  case ESP_BT_GAP_DISC_RES_EVT: {
    bt_keyboard_->handle_bt_device_result(param);
    break;
  }
  case ESP_BT_GAP_KEY_NOTIF_EVT:
    ESP_LOGV(TAG, "BT GAP KEY_NOTIF passkey:%ld", param->key_notif.passkey);
    if (pairing_handler_ != nullptr) (*pairing_handler_)(param->key_notif.passkey);
    break;
  case ESP_BT_GAP_MODE_CHG_EVT:
    ESP_LOGV(TAG, "BT GAP MODE_CHG_EVT mode:%d", param->mode_chg.mode);
    break;
  default:
    ESP_LOGV(TAG, "BT GAP EVENT %s", bt_gap_evt_str(event));
    break;
  }
}

void BTKeyboard::handle_ble_device_result(esp_ble_gap_cb_param_t *param) {
  uint16_t uuid       = 0;
  uint16_t appearance = 0;
  char name[64]       = "";

  uint8_t uuid_len = 0;
  uint8_t *uuid_d =
      esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid_len);

  if (uuid_d != nullptr && uuid_len) {
    uuid = uuid_d[0] + (uuid_d[1] << 8);
  }

  uint8_t appearance_len = 0;
  uint8_t *appearance_d  = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                    ESP_BLE_AD_TYPE_APPEARANCE, &appearance_len);

  if (appearance_d != nullptr && appearance_len) {
    appearance = appearance_d[0] + (appearance_d[1] << 8);
  }

  uint8_t adv_name_len = 0;
  uint8_t *adv_name =
      esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

  if (adv_name == nullptr) {
    adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT,
                                        &adv_name_len);
  }

  if (adv_name != nullptr && adv_name_len) {
    memcpy(name, adv_name, adv_name_len);
    name[adv_name_len] = 0;
  }

  std::cout << "BLE: " << param->scan_rst.bda << ", " << "RSSI: " << std::dec
            << param->scan_rst.rssi << ", "
            << "UUID: 0x" << std::hex << std::setw(4) << std::setfill('0') << uuid << ", "
            << "APPEARANCE: 0x" << std::hex << std::setw(4) << std::setfill('0') << appearance
            << ", "
            << "ADDR_TYPE: " << ble_addr_type_str(param->scan_rst.ble_addr_type);

  if (adv_name_len) {
    std::cout << ", NAME: '" << name << "'";
  }
  std::cout << std::endl;

#if SCAN
  if (uuid == ESP_GATT_UUID_HID_SVC) {
    add_ble_scan_result(param->scan_rst.bda, param->scan_rst.ble_addr_type, appearance, adv_name,
                        adv_name_len, param->scan_rst.rssi);
  }
#endif
}

void BTKeyboard::ble_gap_event_handler(esp_gap_ble_cb_event_t event,
                                       esp_ble_gap_cb_param_t *param) {
  switch (event) {

    // SCAN

  case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
    ESP_LOGV(TAG, "BLE GAP EVENT SCAN_PARAM_SET_COMPLETE");
    SEND_BLE_CB();
    break;
  }
  case ESP_GAP_BLE_SCAN_RESULT_EVT: {
    switch (param->scan_rst.search_evt) {
    case ESP_GAP_SEARCH_INQ_RES_EVT: {
      bt_keyboard_->handle_ble_device_result(param);
      break;
    }
    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
      ESP_LOGV(TAG, "BLE GAP EVENT SCAN DONE: %d", param->scan_rst.num_resps);
      SEND_BLE_CB();
      break;
    default:
      break;
    }
    break;
  }
  case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: {
    ESP_LOGV(TAG, "BLE GAP EVENT SCAN CANCELED");
    break;
  }

    // ADVERTISEMENT

  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGV(TAG, "BLE GAP ADV_DATA_SET_COMPLETE");
    break;

  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    ESP_LOGV(TAG, "BLE GAP ADV_START_COMPLETE");
    break;

    // AUTHENTICATION

  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    if (!param->ble_security.auth_cmpl.success) {
      ESP_LOGE(TAG, "BLE GAP AUTH ERROR: 0x%x", param->ble_security.auth_cmpl.fail_reason);
    } else {
      ESP_LOGV(TAG, "BLE GAP AUTH SUCCESS");
    }
    break;

  case ESP_GAP_BLE_KEY_EVT: // shows the ble key info share with peer device to the user.
    ESP_LOGV(TAG, "BLE GAP KEY type = %s", ble_key_type_str(param->ble_security.ble_key.key_type));
    break;

  case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: // ESP_IO_CAP_OUT
    // The app will receive this evt when the IO has Output capability and the peer device IO has
    // Input capability. Show the passkey number to the user to input it in the peer device.
    ESP_LOGV(TAG, "BLE GAP PASSKEY_NOTIF passkey:%ld", param->ble_security.key_notif.passkey);
    if (pairing_handler_ != nullptr) (*pairing_handler_)(param->ble_security.key_notif.passkey);
    break;

  case ESP_GAP_BLE_NC_REQ_EVT: // ESP_IO_CAP_IO
    // The app will receive this event when the IO has DisplayYesNO capability and the peer device
    // IO also has DisplayYesNo capability. show the passkey number to the user to confirm it with
    // the number displayed by peer device.
    ESP_LOGV(TAG, "BLE GAP NC_REQ passkey:%ld", param->ble_security.key_notif.passkey);
    esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
    break;

  case ESP_GAP_BLE_PASSKEY_REQ_EVT: // ESP_IO_CAP_IN
    // The app will receive this evt when the IO has Input capability and the peer device IO has
    // Output capability. See the passkey number on the peer device and send it back.
    ESP_LOGV(TAG, "BLE GAP PASSKEY_REQ");
    // esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, 1234);
    break;

  case ESP_GAP_BLE_SEC_REQ_EVT:
    ESP_LOGV(TAG, "BLE GAP SEC_REQ");
    // Send the positive(true) security response to the peer device to accept the security request.
    // If not accept the security request, should send the security response with negative(false)
    // accept value.
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    break;

  default:
    ESP_LOGV(TAG, "BLE GAP EVENT %s", ble_gap_evt_str(event));
    break;
  }
}

esp_err_t BTKeyboard::start_bt_scan(uint32_t seconds) {
  esp_err_t ret = ESP_OK;
  if ((ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (int)(seconds / 1.28),
                                        0)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_bt_gap_start_discovery failed: %d", ret);
    return ret;
  }
  return ret;
}

esp_err_t BTKeyboard::esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name) {

  esp_err_t ret;

  const uint8_t hidd_service_uuid128[] = {
      0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
  };

  esp_ble_adv_data_t ble_adv_data = {
      .set_scan_rsp     = false,
      .include_name     = true,
      .include_txpower  = true,
      .min_interval     = 0x0006, // slave connection min interval, Time = min_interval * 1.25 msec
      .max_interval     = 0x0010, // slave connection max interval, Time = max_interval * 1.25 msec
      .appearance       = appearance,
      .manufacturer_len = 0,
      .p_manufacturer_data = NULL,
      .service_data_len    = 0,
      .p_service_data      = NULL,
      .service_uuid_len    = sizeof(hidd_service_uuid128),
      .p_service_uuid      = (uint8_t *)hidd_service_uuid128,
      .flag                = 0x6,
  };

  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
  // esp_ble_io_cap_t iocap = ESP_IO_CAP_OUT;//you have to enter the key on the host
  // esp_ble_io_cap_t iocap = ESP_IO_CAP_IN;//you have to enter the key on the device
  esp_ble_io_cap_t iocap = ESP_IO_CAP_IO; // you have to agree that key matches on both
  // esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;//device is not capable of input or output, insecure
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t key_size = 16;   // the key size should be 7~16 bytes
  uint32_t passkey = 1234; // ESP_IO_CAP_OUT

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param AUTHEN_REQ_MODE failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param IOCAP_MODE failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param SET_INIT_KEY failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param SET_RSP_KEY failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param MAX_KEY_SIZE failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey,
                                            sizeof(uint32_t))) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_security_param SET_STATIC_PASSKEY failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_set_device_name(device_name)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP set_device_name failed: %d", ret);
    return ret;
  }

  if ((ret = esp_ble_gap_config_adv_data(&ble_adv_data)) != ESP_OK) {
    ESP_LOGE(TAG, "GAP config_adv_data failed: %d", ret);
    return ret;
  }

  return ret;
}

esp_err_t BTKeyboard::esp_hid_ble_gap_adv_start(void) {
  static esp_ble_adv_params_t hidd_adv_params = {
      .adv_int_min       = 0x20,
      .adv_int_max       = 0x30,
      .adv_type          = ADV_TYPE_IND,
      .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
      .peer_addr         = {0},
      .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
      .channel_map       = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };
  return esp_ble_gap_start_advertising(&hidd_adv_params);
}

esp_err_t BTKeyboard::start_ble_scan(uint32_t seconds) {
  static esp_ble_scan_params_t hid_scan_params = {
      .scan_type          = BLE_SCAN_TYPE_ACTIVE,
      .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
      .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
      .scan_interval      = 0x50,
      .scan_window        = 0x30,
      .scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE,
  };

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

esp_err_t BTKeyboard::esp_hid_scan(uint32_t seconds, size_t *num_results,
                                   esp_hid_scan_result_t **results) {
  if (num_bt_scan_results_ || bt_scan_results_ || num_ble_scan_results_ || ble_scan_results_) {
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
  *results     = bt_scan_results_;

  if (num_bt_scan_results_) {
    while (bt_scan_results_->next != NULL) {
      bt_scan_results_ = bt_scan_results_->next;
    }
    bt_scan_results_->next = ble_scan_results_;
  } else {
    *results = ble_scan_results_;
  }

  num_bt_scan_results_  = 0;
  bt_scan_results_      = NULL;
  num_ble_scan_results_ = 0;
  ble_scan_results_     = NULL;

  return ESP_OK;
}

void BTKeyboard::devices_scan(int seconds_wait_time) {

  if (connected_) return;

  size_t results_len             = 0;
  esp_hid_scan_result_t *results = NULL;
  ESP_LOGV(TAG, "SCAN...");

  // start scan for HID devices

  esp_hid_scan(seconds_wait_time, &results_len, &results);
  ESP_LOGV(TAG, "SCAN: %u results", results_len);
  if (results_len) {
    esp_hid_scan_result_t *r  = results;
    esp_hid_scan_result_t *cr = NULL;
    uint16_t appearance       = r->ble.appearance;
    while (r) {
      std::cout << "  " << (r->transport == ESP_HID_TRANSPORT_BLE ? "BLE: " : "BT: ") << r->bda
                << std::dec << ", RSSI: " << +r->rssi << ", USAGE: " << esp_hid_usage_str(r->usage);
      if (r->transport == ESP_HID_TRANSPORT_BLE) {
        cr = r;
        std::cout << ", APPEARANCE: 0x" << std::hex << std::setw(4) << std::setfill('0')
                  << appearance << ", ADDR_TYPE: '" << ble_addr_type_str(r->ble.addr_type) << "'"
                  << std::dec;
      }
      if (r->transport == ESP_HID_TRANSPORT_BT) {
        cr = r;
        std::cout << ", COD: " << esp_hid_cod_major_str(r->bt.cod.major) << "[";
        esp_hid_cod_minor_print(r->bt.cod.minor, stdout);
        std::cout << "] srv 0x" << std::hex << std::setw(3) << std::setfill('0')
                  << r->bt.cod.service << ", " << r->bt.uuid;
      }

      if (r->name) {
        std::cout << ", NAME: " << r->name << std::endl;
      } else {
        std::cout << std::endl;
      }
      r = r->next;
    }
    if (cr) {
      // open the last result
      esp_hidh_dev_open(cr->bda, cr->transport, cr->ble.addr_type);
    }
    // free the results
    esp_hid_scan_results_free(results);
  }
}

void BTKeyboard::hidh_callback(void *handler_args, esp_event_base_t base, int32_t id,
                               void *event_data) {
  esp_hidh_event_t event       = (esp_hidh_event_t)id;
  esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

  switch (event) {
  case ESP_HIDH_OPEN_EVENT: {
    if (param->open.status == ESP_OK) {
      const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
      ESP_LOGV(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda),
               esp_hidh_dev_name_get(param->open.dev));
      esp_hidh_dev_dump(param->open.dev, stdout);
      set_connected(true);
    } else {
      ESP_LOGE(TAG, " OPEN failed!");
      set_connected(false);
    }
    break;
  }
  case ESP_HIDH_BATTERY_EVENT: {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
    ESP_LOGV(TAG, ESP_BD_ADDR_STR " BATTERY: %d%%", ESP_BD_ADDR_HEX(bda), param->battery.level);
    bt_keyboard_->set_battery_level(param->battery.level);
    break;
  }
  case ESP_HIDH_INPUT_EVENT: {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->input.dev);
    ESP_LOGV(TAG,
             ESP_BD_ADDR_STR " INPUT: %8s, MAP: %2u, ID: %3u, Len: %d, Data:", ESP_BD_ADDR_HEX(bda),
             esp_hid_usage_str(param->input.usage), param->input.map_index, param->input.report_id,
             param->input.length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->input.data, param->input.length, ESP_LOG_DEBUG);
    bt_keyboard_->push_key(param->input.data, param->input.length);
    break;
  }
  case ESP_HIDH_FEATURE_EVENT: {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
    ESP_LOGV(TAG, ESP_BD_ADDR_STR " FEATURE: %8s, MAP: %2u, ID: %3u, Len: %d", ESP_BD_ADDR_HEX(bda),
             esp_hid_usage_str(param->feature.usage), param->feature.map_index,
             param->feature.report_id, param->feature.length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->feature.data, param->feature.length, ESP_LOG_DEBUG);
    break;
  }
  case ESP_HIDH_CLOSE_EVENT: {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
    ESP_LOGV(TAG, ESP_BD_ADDR_STR " CLOSE: %s", ESP_BD_ADDR_HEX(bda),
             esp_hidh_dev_name_get(param->close.dev));
    set_connected(false);
    break;
  }
  default:
    ESP_LOGV(TAG, "EVENT: %d", event);
    break;
  }
}

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
        // ESP_LOGI(TAG, "Scan code: %d", ch);
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

void BTKeyboard::show_bonded_devices(void) {
  int dev_num = esp_ble_get_bond_device_num();
  if (dev_num == 0) {
    ESP_LOGI(TAG, "Bonded devices number zero\n");
    return;
  }

  esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
  if (!dev_list) {
    ESP_LOGI(TAG, "malloc failed, return\n");
    return;
  }
  esp_ble_get_bond_device_list(&dev_num, dev_list);
  ESP_LOGI(TAG, "Bonded devices number %d", dev_num);
  for (int i = 0; i < dev_num; i++) {
    ESP_LOGI(TAG, "[%u] addr_type %u, addr " ESP_BD_ADDR_STR "", i, dev_list[i].bd_addr_type,
             ESP_BD_ADDR_HEX(dev_list[i].bd_addr));
  }

  free(dev_list);
}

void BTKeyboard::remove_all_bonded_devices() {
  int dev_num = esp_ble_get_bond_device_num();
  if (dev_num == 0) {
    ESP_LOGI(TAG, "Bonded devices number zero\n");
    return;
  }

  esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
  if (!dev_list) {
    ESP_LOGI(TAG, "malloc failed, return\n");
    return;
  }
  esp_ble_get_bond_device_list(&dev_num, dev_list);
  for (int i = 0; i < dev_num; i++) {
    esp_ble_remove_bond_device(dev_list[i].bd_addr);
  }

  free(dev_list);
}
