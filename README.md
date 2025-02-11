# ESP32 ESP-IDF Bluetooth Keyboard Input Demo For Receiving And Processing Keyboard Inputs

(Updated 2025-02-10)

- Now using `ESP-IDF V5.4` for development. 
- Now using C++20
- Partial support of CMake Presets (compiling using VSCode CMake extensions instead of idf.py). `idf.py` is still used for flashing and monitoring.
- For the `setup()` method, added handler pointers when connection completed and connection lost.
- The `BTKeyboard` class is now located in the `components` folder.
- Added `show_bonded_devices()` and `remove_all_bonded_devices()` methods. Useful for debugging.
- `keys` renamed to `keys_data` and is now 20 bytes in size. It may need to be larger depending on the keyboard in use.
- C++ flavors: All printf use is gone. Now using ostream. Using std::string, std::forward_list and automatic pointers management.
- BLE keyboard `Logitech MX Keys Mini` is able to do pairing. Still doesn't reconnect through the bonding process (connection recovery doesn't work, still investigating).
- BT keyboard `Logitech K380` works well in both pairing and bonding (recovering after signal lost or ESP32 reboot).
- Apple Wireless keyboard not working.

----

This is a demonstration of an external Bluetooth keyboard sending characters to an ESP32. The code is mainly based on the ESP-IDF's bluetooth/esp_hid_host example, packaged into a class with added support for easier integration with a user application. 

Please look at the `main/main.cpp` file on how to use the class. Only one instance of the class should be used to avoid conflicts and resource limitations within the Bluetooth stack.

The class named BTKeyboard waits for a keyboard to be available for pairing through the `BTKeyboard::devices_scan()` method (must be called by the application). It will then accumulate scan codes in a queue to be processed. The class methods available allow for:
- Retrieval of the low-level key scan codes transmitted by the keyboard (`bool wait_for_low_event(BTKeyboard::KeyInfo & inf)` method)
- Retrieval of the ASCII characters augmented with function keys values (`char wait_for_ascii_char()` or `char get_ascii_char()` methods). 

The following table lists the character values returned (support of other keyboard keys may be added in a future release) through the `wait_for_ascii_char()` and `get_ascii_char()` methods:

| Values      | Description      |
|:-----------:|------------------|
| 0x00        | No key received (returned by `get_ascii_char()` when no key available) |
| 0x01 - 0x1A | Ctrl-A .. Ctrl-Z |
| 0x08        | Backspace key    |
| 0x09        | Tab key          |
| 0x0D        | Return key       |
| 0x1B        | ESC key          |
| 0x20 - 0x7E | ASCII codes      |
| 0x7F        | Delete key       |
| 0x80        | CAPS Lock        |
| 0x81 - 0x8C | F1 .. F12        |
| 0x8D        | PrintScreen key  |
| 0x8E        | ScrollLock key   |
| 0x8F        | Pause key        |
| 0x90        | Insert key       |
| 0x91        | Home key         |
| 0x92        | PageUp key       |
| 0x93        | End key          |
| 0x94        | PageDown key     |
| 0x95        | RightArrow key   |
| 0x96        | LeftArrow key    |
| 0x97        | DownArrow key    |
| 0x98        | UpArrow key      |

The returned scan codes in the BTKeyboard::KeyInfo structure are defined in chapter 10 of the [USB HID Usage Tables document](https://usb.org/sites/default/files/hut1_22.pdf) and are typically provided directly by the keyboard. The BTKeyboard class supports up to three keys pressed at the same time. The corresponding scan codes are located in the `keys_data` field. The `modifier` field contains the CTRL/SHIFT/ALT/META left and right key modifier info.

The main program is a simple demonstration of the usage of the class. The author is using a Logitech K380 (standard Bluetooth keyboard, not a BLE) and the recent Logitech MX Keys Mini (BLE) keyboards for this demo. Other Bluetooth keyboards may work but may require some modification (mainly adding other key scan codes). 

The first-generation Apple wireless keyboard is known to not function correctly.

The `sdkconfig.defaults` file specifies the ESP-IDF sdkconfig parameters required for this demo to work.

A bug in `ESP-IDF 4.3.x` may cause an internal stack overflow due to improper Bluetooth stack resource handling. This issue has been fixed in `ESP-IDF 4.4`.

Testing with `ESP-IDF V5.4` appears to be stable. 

Ensure that you have installed ESP-IDF V4.4 or later (example provided is for Linux/MacOS and V5.4):

```
cd ~/esp
git clone -b release/v5.4 --recursive https://github.com/espressif/esp-idf.git
```

... and periodically update it to the latest version using `git pull`:

```
cd ~/esp/esp-idf
git pull
git submodule update --init --recursive
```

### Some work that remains to be done:

- [x] Add pairing code retrieval by the application.
- [x] CapsLock management
- [ ] Synchronize the code with the `esp_hid_host` example of ESP-IDF
- [ ] Some more key mapping
- [ ] Re-introduce conditional compilation for BT and BLE
- [x] Isolate the class into an ESP-IDF Component

### Issue:

- BLE Keyboard's connection (`MX Keys Mini`) recovering doesn't seem to work. Still investigating.