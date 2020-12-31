#include "main.h"
#include <Preferences.h>

WeatherData wdata = {};

// Sliding window buffer to calculate the average wind speed over a period of two minutes
#define RT_AVG_MAX  (120 / PERIOD_5_SEC)
static float rt_avg[RT_AVG_MAX] = {};
static uint32_t rt_avg_next = 0;

// Sliding window buffer to keep individual peak winds over a period of two minutes
#define RT_PEAK_MAX  (120 / PERIOD_PEAK_WIND_SEC)
static float rt_peak[RT_PEAK_MAX] = {};
static uint32_t rt_peak_next = 0;

// Sliding window buffer to calculate the average wind direction over a period of two minutes
static float rt_wdir_ew[RT_AVG_MAX] = {}; // Wind direction: East-West coordinate
static float rt_wdir_ns[RT_AVG_MAX] = {}; // Wind direction: North-South coordinate
static uint32_t rt_wdir_next = 0;

// Sliding window buffer to keep detected rain tip values (new rain) for the rain rate calculation over a period of ten minutes
#define RT_RAIN_MAX  (PERIOD_RAIN_RATE / PERIOD_5_SEC)
static uint8_t rt_rain[RT_RAIN_MAX] = {}; // Using a byte array since we can't have more than a few tips in a 5-sec period
static uint32_t rt_rain_next = 0;

// Look up tables for wind direction polar system transformation, we only read 16 directions from the wind vane
static const float tbl_sin[16] = {
     0.000000, 0.382683, 0.707107, 0.923880, 1.000000, 0.923880, 0.707107, 0.382683,
     0.000000,-0.382683,-0.707107,-0.923879,-1.000000,-0.923879,-0.707107,-0.382683 };
static const float tbl_cos[16] = {
     1.000000, 0.923880, 0.707107, 0.382683, 0.000000,-0.382683,-0.707107,-0.923880,
    -1.000000,-0.923880,-0.707107,-0.382684, 0.000000, 0.382684, 0.707107, 0.923880 };

static Preferences pref;

// Set a preference string value pairs, we are using int, float and string variants
void pref_set(const char* name, uint32_t value)
{
    pref.begin("wd", false);
    pref.putUInt(name, value);
    pref.end();
}

void pref_set(const char* name, float value)
{
    pref.begin("wd", false);
    pref.putFloat(name, value);
    pref.end();
}

void pref_set(const char* name, String value)
{
    pref.begin("wd", false);
    pref.putString(name, value);
    pref.end();
}

static void vTask_read_sensors(void *p)
{
    float hz, mph;

    // Make this task sleep and awake once a second
    const TickType_t xFrequency = 1 * 1000 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        // Wait for the next cycle first, all calculation below will be triggered after the initial period passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        wdata.seconds++;

        // Once an hour, adjust rain event counters and possibly reset rain_event value
        if ((wdata.seconds % PERIOD_1_HR) == 0)
        {
            wdata.rain_event_cnt += 1; // Increment the counter by one hour and make sure it is safely stored in the NVM
            pref_set("rain_event_cnt", wdata.rain_event_cnt);
            // Reset the rain_event only when the time since the last rain equals the reset limit. The counter will keep
            // incrementing showing the number of hours since the last rain even after it had zeroed out the rain_event
            if (wdata.rain_event_cnt == wdata.rain_event_max)
            {
                wdata.rain_event = 0;
                pref_set("rain_event", wdata.rain_event);
            }
        }

        // Once every 3 seconds, calculate individual peak wind
        if ((wdata.seconds % PERIOD_PEAK_WIND_SEC) == 0)
        {
            uint32_t count2 = anem.get_and_clear_count2();
            hz = float(count2) / PERIOD_PEAK_WIND_SEC;
            mph = hz * wdata.wind_calib;

            // Store the new 3-sec wind peak value into a circular buffer
            rt_peak[rt_peak_next] = mph;
            rt_peak_next = (rt_peak_next + 1) % RT_PEAK_MAX;
        }

        // Once every 5 seconds, read all sensors and recalculate relevant data
        if ((wdata.seconds % PERIOD_5_SEC) == 0)
        {
            // Fill up WeatherData fields with the sensors' (and computed) data

            // Read temperature, humidity and pressure sensor
            read_bme280();

            // Find the max peak wind over the size of its circular buffer, a 2-minute sliding window
            float wind_peak = rt_peak[0];
            for (int i = 1; i < RT_PEAK_MAX; i++)
            {
                if (rt_peak[i] > wind_peak)
                    wind_peak = rt_peak[i];
            }
            wdata.wind_peak = wind_peak;

            // Calculate wind realitime, average over a 5-sec sampling period
            uint32_t count = anem.get_and_clear_count();
            wdata.anem_count = count;
            hz = float(count) / PERIOD_5_SEC;
            mph = hz * wdata.wind_calib;
            wdata.wind_rt = mph;

            // Store the new 5-sec real-time wind into a circular buffer
            rt_avg[rt_avg_next] = wdata.wind_rt;
            rt_avg_next = (rt_avg_next + 1) % RT_AVG_MAX;

            // Calculate average wind speed over the size of its circular buffer, a 2-minute sliding window
            float sum = rt_avg[0];
            for (int i = 1; i < RT_AVG_MAX; i++)
                sum += rt_avg[i];
            wdata.wind_avg = sum / RT_AVG_MAX;

            // Get and store wind vane direction
            wdata.wind_dir_adc = read_wind_dir_adc();
            wdata.wind_dir_rt = wind_calc_dir(wdata.wind_dir_adc);

            // Make a wind vane vector from (direction, wind speed)
            uint32_t wdir = wdata.wind_dir_rt;
            float wrt = wdata.wind_rt;

            // Store the new 5-sec real-time wind direction into a circular buffer
            rt_wdir_ew[rt_wdir_next] = wrt * tbl_sin[wdir];
            rt_wdir_ns[rt_wdir_next] = wrt * tbl_cos[wdir];
            rt_wdir_next = (rt_wdir_next + 1) % RT_AVG_MAX;

            // Calculate the average of wind directional vectors, a 2-minute sliding window
            // http://www.webmet.com/met_monitoring/622.html
            float wdir_ew = rt_wdir_ew[0];
            float wdir_ns = rt_wdir_ns[0];
            for (int i = 1; i < RT_AVG_MAX; i++)
            {
                wdir_ew += rt_wdir_ew[i];
                wdir_ns += rt_wdir_ns[i];
            }
            wdir_ew /= RT_AVG_MAX;
            wdir_ns /= RT_AVG_MAX;
            // From the (x,y) that is (ew,ns), compute the vector angle of the final average wind direction
            float angle = atan2(wdir_ew, wdir_ns) * RAD_TO_DEG;
            if (angle < 0)
                angle = angle + 360;
            wdata.wind_dir_avg = int(angle) % 360;

            // Read the current rain gauge tip counter for any tips accumulated in the last 5-sec interval
            uint32_t rain_count = rain.get_and_clear_count();
            wdata.rain_test += rain_count; // Always increment the test counter
            // If the relative humidity was less than a cutoff value, it is a false rain positive and will be ignored
            // However, if the BME humidity sensor could not be read, ignore that check
            if ((wdata.error & ERROR_BME_READ) || (uint32_t(wdata.humidity) >= CAN_RAIN_HUMIDITY_MIN))
            {
                // Store the new rain tip amount into a circular buffer
                rt_rain[rt_rain_next] = uint8_t(rain_count);
                rt_rain_next = (rt_rain_next + 1) % RT_RAIN_MAX;

                // Calculate the sum of all new rain tips for the past 10-min and publish it as a rain rate
                uint32_t rain_sum = uint32_t(rt_rain[0]);
                for (int i = 1; i < RT_RAIN_MAX; i++)
                    rain_sum += uint32_t(rt_rain[i]);
                wdata.rain_rate = rain_sum * RAIN_RATE_MUL; // Publish it as "per hour" value

                // Add the new rain volume to the current rain_event and the overall rain total and back the result up in the NVM
                // Do it only if there is new rain to add so that we don't write to NVM unnecessarily
                if (rain_count)
                {
                    wdata.rain_event += rain_count;
                    wdata.rain_total += rain_count;
                    wdata.rain_event_cnt = 0; // Restart 'the number of hours since the last rain' counter
                    pref_set("rain_event", wdata.rain_event);
                    pref_set("rain_total", wdata.rain_total);
                    pref_set("rain_event_cnt", wdata.rain_event_cnt);
                }
            }
#ifdef TEST
            Serial.print(wdata.seconds);
            Serial.print(": ");
            Serial.print(wdata.temp_c);
            Serial.print(" C ");
            Serial.print(wdata.temp_f);
            Serial.print(" F ");
            Serial.print(wdata.pressure);
            Serial.print(" hPa HUM: ");
            Serial.print(wdata.humidity);
            Serial.print(" % PEAK: ");
            Serial.print(wdata.wind_peak);
            Serial.print(" mph RT: ");
            Serial.print(wdata.wind_rt);
            Serial.print(" mph AVG: ");
            Serial.print(wdata.wind_avg);
            Serial.print(" mph DIR: ");
            Serial.print(wdata.wind_dir_rt);
            Serial.print(" DIR_AVG: ");
            Serial.print(wdata.wind_dir_avg);
            Serial.print(" deg RAIN: ");
            Serial.print(wdata.rain_rate);
            Serial.print(" EVENT: ");
            Serial.print(wdata.rain_event);
            Serial.print(" TOT: ");
            Serial.print(wdata.rain_total);
            Serial.println(" RTEST: ");
            Serial.print(wdata.rain_test);
            Serial.println("");
#endif // TEST
        }
        // At the end, preset various response strings that the server should give out. This will happen once a second,
        // whether we have any new data or not.
        webserver_set_response();
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    // Configure strap pins for input
    pinMode(36, INPUT);
    pinMode(39, INPUT);

    // Read the initial rain values stored in the NVM
    pref.begin("wd", true);
    wdata.id = pref.getString("id", "");
    wdata.tag = pref.getString("tag", "");
    wdata.wind_calib = pref.getFloat("wind_calib", WIND_FACTOR_MPH);
    wdata.rain_calib = pref.getFloat("rain_calib", RAIN_FACTOR_IN);
    wdata.rain_event = pref.getUInt("rain_event", 0);
    wdata.rain_event_max = pref.getUInt("rain_event_max", 24);
    wdata.rain_event_cnt = pref.getUInt("rain_event_cnt", 0);
    wdata.rain_total = pref.getUInt("rain_total", 0);
    pref.end();

    setup_bme280();
    setup_wind_rain();
    setup_wifi();
    setup_webserver();

    // Arduino loop is running on core 1 and priority 1
    // https://techtutorialsx.com/2017/05/09/esp32-running-code-on-a-specific-core
    xTaskCreatePinnedToCore(
        vTask_read_sensors, // Task function
        "task_sensors",     // String with name of the task
        2048,               // Stack size in bytes
        &wdata,             // Parameter passed as input to the task is the global weather data struct (not used)
        1,                  // Priority of the task
        NULL,               // Task handle
        1);                 // Core where the task should run (user program core)
}

void loop()
{
    wifi_check_loop();
}
