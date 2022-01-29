# ESP32 ESP-IDF Bluetooth keyboard input demo

(Updated 2022/01/29)

(Work in progress... updated for ESP-IDF V4.4)

This is a demonstration of an external Bluetooth keyboard sending characters to an ESP32.

The class named BTKeyboard waits for a keyboard to be available for pairing through the `BTKeyboard::devices_scan()` method (must be called by the application). It will then cumulate scan codes in a queue to be processed. The class methods available allow for the retrieval of the low-level key scan codes transmitted by the keyboard (`bool wait_for_low_event(BTKeyboard::KeyInfo & inf)` method) or the ASCII characters augmented with function keys values (`char wait_for_ascii_char()` or `char get_ascii_char()` methods). The following list the character values returned (support of other keyboard keys may be added in a future release):

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

The returned scan codes in the BTKeyboard::KeyInfo structure are defined in chapter 10 of https://usb.org/sites/default/files/hut1_22.pdf and are usually supplied directly by the keyboard. The BTKeyboard class supports up to three keys pressed at the same time. The corresponging scan codes are located in the `keys[3]` field. The `modifier` field contains the CTRL/SHIFT/ALT/META left and right key modifier info.

The main program is a simple demonstration of the usage of the class. The author is using a Logitech K380 keyboard (standard Bluetooth keyboard, not a BLE) for this demo. Other Bluetooth keyboards may work but may require some modification (mainly adding other key scan codes).

The `sdkconfig.defaults` file identifies the ESP-IDF sdkconfig parameters that are required to have this demo working.

A bug with ESP-IDF 4.3.x may cause an internal stack overflow. Seems to be corrected in 4.4.

This is not ready yet as testing with ESP-IDF 4.4 is ongoing. Seems to work fine at this point in time. 

Please be sure that you have installed the ESP-IDF v4.4 (examples are for Linux/MacOS):

```
cd ~/esp
git clone -b release/v4.4 --recursive https://github.com/espressif/esp-idf.git
```

... and update it to the last v4.4 updates from time to time (git pull):

```
cd ~/esp/esp-idf
git pull
git submodule --update --init --recursive
```

Some work remains to be done:

- Add pairing code retrieval by the application.
- Some more key mapping
