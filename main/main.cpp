// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#include "bt_keyboard.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <iostream>

BTKeyboard bt_keyboard;

void pairing_handler(uint32_t pid) {
  std::cout << "Please enter the following pairing code, " << std::endl
            << "followed with ENTER on your keyboard: " << pid << std::endl;
}

extern "C" {

void app_main() {
  esp_err_t ret;

  // To test the Pairing code entry, uncomment the following line as pairing info is
  // kept in the nvs. Pairing will then be required on every boot.
  // ESP_ERROR_CHECK(nvs_flash_erase());

  ret = nvs_flash_init();
  if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (bt_keyboard.setup(pairing_handler)) { // Must be called once
    bt_keyboard.devices_scan();             // Required to discover new keyboards and for pairing
                                            // Default duration is 5 seconds
    while (true) {
#if 0 // 0 = scan codes retrieval, 1 = augmented ASCII retrieval
          uint8_t ch = bt_keyboard.wait_for_ascii_char();
          // uint8_t ch = bt_keyboard.get_ascii_char(); // Without waiting

          if ((ch >= ' ') && (ch < 127)) std::cout << ch << std::flush; 
          else if (ch > 0) {
            std::cout << '[' << +ch << ']' << std::flush;
          }
#else
      BTKeyboard::KeyInfo inf;

      bt_keyboard.wait_for_low_event(inf);

      std::cout << "RECEIVED KEYBOARD EVENT: ";
      for (int n = 0; n < inf.size; n++) {
        std::cout << std::hex << +inf.keys[n] << ", ";
      }
      std::cout << std::endl;
#endif
    }
  }
}
}