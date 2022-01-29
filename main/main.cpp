// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#include "globals.hpp"

#include "nvs_flash.h"
#include "bt_keyboard.hpp"

#include <iostream>

extern "C" {

  void app_main() 
  {
    esp_err_t ret;
    
    ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret); 

    if (bt_keyboard.setup()) {
      bt_keyboard.devices_scan();

      while (true) {
        #if 1
          uint8_t ch = bt_keyboard.wait_for_ascii_char();

          if ((ch >= ' ') && (ch < 127)) std::cout << ch << std::flush; 
          else  {
            std::cout << '[' << +ch << ']' << ch << std::flush;
            printf("[%d]", ch);
            putchar(ch);
          }
        #else
          BTKeyboard::KeyInfo inf;
          
          bt_keyboard.wait_for_low_event(inf);

          std::cout << "RECEIVED KEYBOARD EVENT: "
                    << std::hex
                    << "Mod: "
                    << + (uint8_t) inf.modifier
                    << ", Keys: "
                    << +inf.keys[0] << ", "
                    << +inf.keys[1] << ", "
                    << +inf.keys[2] << std::endl;
        #endif

      }
    }
  }

}