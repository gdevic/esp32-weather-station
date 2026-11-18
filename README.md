# esp32-weather-station
ESP32 based weather station using BME280/DHT22 and Argent assembly 80422
This project uses EZSBC "ESP32 Breakout and Development Board": https://www.ezsbc.com/product/esp32-breakout-and-development-board/

If you need to add support for ESP32, copy and paste the following line to the Additional Boards Manager URLs field:
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

Set it up as a "ESP32 Dev Module"

It needs these additional libraries which can be installed via Arduinio IDE Library Manager:
* // https://github.com/dvarrel/ESPAsyncWebSrv
* // https://github.com/dvarrel/AsyncTCP
* // https://github.com/dvarrel/DHT22

Set your WiFi credentials in "wifi_credentials.h"

OTA update:
Sketch->Export compiled binary->select "esp32-weather-station.ino.bin"

Builds from 1.22 on use the ESP32 Arduino core 3.0.7 (ESP-IDF 5.1); the images in `build_archive/` were built with it.

Virtual rain tips:
`http://<IP>/set?rain_tips=N` (N up to 255) makes the station count N rain gauge tips at its next 5-second cycle, exactly as if the gauge had tipped: `rain_rate`, `rain_event`, `rain_event_cnt` and `rain_total` follow as usual. The value is not stored in NVM. The status page `/` shows the tips received since boot as `rain_virtual`; `rain_test` keeps counting only real reed switch closures. A server can use this to stand in for a missing or broken rain gauge.
