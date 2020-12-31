#include <Arduino.h>
#include <Wire.h>

// The version string shown in stats. Nothing depends on it and it is used only to confirm newly flashed firmware.
#define FIRMWARE_VERSION "1.16"

// To update firmware OTA, point your web server to <IP>/upload device webpage
// Static IP address depends on the GPIO36/GPIO39 strapped to GND or Vcc in this way:
// GPIO39 GPIO36   IP
//   0      0    192.168.1.32
//   0      1    192.168.1.33
//   1      0    192.168.1.34
//   1      1    192.168.1.35
// To simply test the code, define TEST and use a hard-coded ip ending with "99"
//#define TEST

// Period, in seconds, of one hour; to count when to clear the rain_event
#define PERIOD_1_HR  (60 * 60)

// Period, in seconds, to read sensors; and to calculate wind stats
#define PERIOD_5_SEC  5

// "The peak wind is a measurement of a burst or a gust of wind that one feels over a very short period of time.
//  In the case of the peak wind, the very short period of time is usually three seconds."
// http://prugarinc.com/weather-events/peak-wind-vs-maximum-wind-whats-the-difference
#define PERIOD_PEAK_WIND_SEC  3

// Rain rate calculation is based on a 10-min sliding window
#define PERIOD_RAIN_RATE  (60 * 10)
#define RAIN_RATE_MUL      6 // Multiplication factor to publish rain rate, to bring it to "per hour" value

// This value deals with false rain positives when rain gauge tips due to various extraneous reasons
// We are going to ignore any rain counts if the relative humidity is below this percentage since any real rain
// would evaporate before reaching the ground. The reason this is not 100 (%) is to deal with possible corner cases.
// XXX Quick summer rains can happen even when the humidity is quite low, so this check becomes at best questionable.
#define CAN_RAIN_HUMIDITY_MIN  50

#define WIND_FACTOR_MPH 1.492 // Relay tick to mph
#define RAIN_FACTOR_IN  0.021 // Relay tick to inches of rain (initial best guess calibration value, rain_calib)

struct WeatherData
{
    // Variables marked with [NV] are held in the non-volatile memory using Preferences
    // The station does not do anything with the id and the tag; clients should use them to identify and name a station
    String id;          // [NV] Station identification string, held in the non-volatile memory
    String tag;         // [NV] Station description or a tag, held in the non-volatile memory
    float temp_c;       // Current temperature in "C"
    float temp_f;       // Current temperature in "F"
    float pressure;     // Current pressure in "hPa"
    float humidity;     // Current relative humidity in "%"

    float wind_calib;   // [NV] Wind calibration factor (ticks to mph)
    float wind_peak;    // Wind peak maximum value over a 2-min sliding window
    float wind_rt;      // Wind realtime (5-sec averages)
    float wind_avg;     // Wind speed average over a 5-sec sliding window
    int wind_dir_adc;   // Wind direction sensor ADC raw value
    int wind_dir_rt;    // Wind instantaneous, real time direction measured once every 5 sec
    int wind_dir_avg;   // Wind direction [0,360) averaged over a 2-min sliding window

    // The station does not do anything with "rain_calib"; clients should use it as a single calbration reference
    // when converting from the tip counters to inches of rain
    float rain_calib;        // [NV] Rain calibration factor (ticks to in)
    uint32_t rain_total;     // [NV] Rain total tip counter
    uint32_t rain_event;     // [NV] Rain event tip counter
    uint32_t rain_event_max; // [NV] The number of hours after which the station will reset the rain_event
    uint32_t rain_event_cnt; // [NV] The number of hours since the last rain, to reset the rain_event
    uint32_t rain_rate;      // Rain rate, sum of individual new rain tips over a 10-min sliding window times "per hour"
    uint32_t rain_test;      // Rain test counter, unconditionally increments

    // Misc logging and debug fields
    uint32_t seconds;     // Uptime seconds counter (shown as "uptime" in web reports)
    uint32_t anem_count;  // Anemometer count over the last 5 sec
    uint32_t error;       // Bitfield where a non-zero bit indicates a particular error
#define ERROR_BME_INIT  0x00000001  // Error initializing BME sensor
#define ERROR_BME_READ  0x00000002  // Error reading BME sensor value
#define ERROR_SEM_1     0x00010000  // Semaphore timed out (location 1)
#define ERROR_SEM_2     0x00020000  // Semaphore timed out (location 2)
};

extern WeatherData wdata;

class Gauge
{
public:
    Gauge();
    void IRAM_ATTR isr();
    uint32_t get_count() { return count; }
    uint32_t get_and_clear_count() { uint32_t n = count; count = 0; return n; }
    uint32_t get_and_clear_count2() { uint32_t n = count2; count2 = 0; return n; }

private:
    uint32_t last_time; // Used to debounce the reed switch
    uint32_t count; // Counts the number of ISR ticks
    uint32_t count2; // Second counter used for wind peak
    portMUX_TYPE lock;
};

extern Gauge anem;
extern Gauge rain;

// From main.cpp
void pref_set(const char* name, uint32_t value);
void pref_set(const char* name, float value);
void pref_set(const char* name, String value);

// From webserver.cpp
void webserver_set_response();
void setup_wifi();
void setup_webserver();
void wifi_check_loop();

// From bme280.cpp
void read_bme280();
void setup_bme280();

// From argent80422.cpp
void setup_wind_rain();
int read_wind_dir_adc();
int wind_calc_dir(int adc);
