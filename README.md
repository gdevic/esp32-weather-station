# esp32-weather-station
ESP32 based weather station using BME280 and Argent assembly 80422
This project uses EZSBC "ESP32 Breakout and Development Board": https://www.ezsbc.com/product/esp32-breakout-and-development-board/

If you need to add support for ESP32, copy and paste the following line to the Additional Boards Manager URLs field:
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

Set it up as a "ESP32 Dev Module"

It needs two additional libraries which can be installed via Arduinio IDE Library Manager:
* // https://github.com/dvarrel/ESPAsyncWebSrv
* // https://github.com/dvarrel/AsyncTCP

Set your WiFi credentials in "wifi_credentials.h"
