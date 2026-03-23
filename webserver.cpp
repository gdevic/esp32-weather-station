#include "main.h"
#include <WiFi.h>
#include <ESPAsyncWebSrv.h>
#include <Update.h>

// Async web server needs these additional libraries (install via Arduinio IDE Library Manager):
// https://github.com/dvarrel/ESPAsyncWebSrv
// https://github.com/dvarrel/AsyncTCP

extern "C" uint8_t temprature_sens_read(); // Very imprecise internal ESP32 temperature value in F

#ifdef TEST
#define GET_IP4  (99)
#else
#define GET_IP4  (0x20 | (!!digitalRead(32) << 2) | (!!digitalRead(39) << 1) | !!digitalRead(36))
#endif // TEST

#include "wifi_credentials.h"
// This include file is ignored by git (in .gitignore) and should contain your specific ssid and password as defines:
// #define MY_SSID "your-ssid"
// #define MY_PASS "your-password"
static const char* ssid = MY_SSID;
static const char* password = MY_PASS;
static String webtext_root; // Web response to / (root)
static String webtext_json; // Web response to /json
static uint32_t reconnects = 0; // Count how many times WiFi had to reconnect (for stats)
static String wifi_mac; // WiFi MAC address of this station
static SemaphoreHandle_t webtext_semaphore; // Semaphore guarding the access to webtext strings as we are building them
static uint32_t last_request_sec = 0; // Uptime timestamp of the last successfully served request
static volatile bool ota_restart_pending = false;

AsyncWebServer server(80);

String get_uptime_str(uint32_t sec)
{
    uint32_t seconds = (sec % 60);
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t days = sec / 86400;
    return String(days) + ":" + String(hours) + ":" + String(minutes) + ":" + String(seconds);
}

void webserver_set_response()
{
    // Wait 20 ms before giving up. In practice, there are no other tasks that could block this sem. for longer than that
    // This task will take the longest due to a number of string operations
    if (xSemaphoreTake(webtext_semaphore, TickType_t(20)) != pdTRUE)
    {
        wdata.error |= ERROR_SEM_1; // Log this error since we do want to know if 20 ms is ever hit
        return;
    }

    // Make this web page auto-refresh every 5 sec
    webtext_root = String("<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"5\"></head><body><pre>");
    webtext_root.reserve(1024);

    webtext_root += "\nVER = " + String(FIRMWARE_VERSION);
    webtext_root += "\nID = " + wdata.id;
    webtext_root += "\nTAG = " + wdata.tag;
    webtext_root += "\nMAC = " + wifi_mac;
    webtext_root += "\nuptime = " + get_uptime_str(wdata.seconds);
    webtext_root += "\nreconnects = " + String(reconnects);
    webtext_root += "\nSSID = " MY_SSID;
    webtext_root += "\nRSSI = " + String(WiFi.RSSI()); // Signal strength
    webtext_root += "\nGPIO_32/39/36 = " + String(digitalRead(32)) + String(digitalRead(39)) + String(digitalRead(36));
    webtext_root += "\nINT_C = " + String((temprature_sens_read() - 32) / 1.8);
    webtext_root += "\nanem_count = " + String(wdata.anem_count);
    webtext_root += "\nerror = " + String(wdata.error, HEX);

    webtext_root += "\ntemp_c_calib = " + String(wdata.temp_c_calib, 2);
    webtext_root += "\ntemp_c = " + String(wdata.temp_c);
    webtext_root += "\ntemp_f = " + String(wdata.temp_f);
    webtext_root += "\npressure = " + String(wdata.pressure);
    webtext_root += "\nhumidity = " + String(wdata.humidity);   

    webtext_root += "\nwind_calib = " + String(wdata.wind_calib, 4); // More decimal places
    webtext_root += "\nwind_peak = " + String(wdata.wind_peak);
    webtext_root += "\nwind_rt = " + String(wdata.wind_rt);
    webtext_root += "\nwind_avg = " + String(wdata.wind_avg);
    webtext_root += "\nwind_dir_adc = " + String(wdata.wind_dir_adc);
    webtext_root += "\nwind_dir_rt = " + String(wdata.wind_dir_rt);
    webtext_root += "\nwind_dir_avg = " + String(wdata.wind_dir_avg);

    webtext_root += "\nrain_calib = " + String(wdata.rain_calib, 4); // More decimal places
    webtext_root += "\nrain_rate = " + String(wdata.rain_rate);
    webtext_root += "\nrain_event = " + String(wdata.rain_event);
    webtext_root += "\nrain_event_max = " + String(wdata.rain_event_max);
    webtext_root += "\nrain_event_cnt = " + String(wdata.rain_event_cnt);
    webtext_root += "\nrain_total = " + String(wdata.rain_total);
    webtext_root += "\nrain_test = " + String(wdata.rain_test);

    webtext_root += String("</pre></body></html>\n");

    // Format the json response
    webtext_json = "{";
    webtext_json.reserve(512);
    webtext_json += " \"id\":\"" + wdata.id + "\"";
    webtext_json += ", \"tag\":\"" + wdata.tag + "\"";
    webtext_json += ", \"uptime\":" + String(wdata.seconds);
    // When out of reset, and until the very fist time we had a chance to read sensors and calculate some meaningful
    // values, do not attempt to return any data nodes
    if (wdata.seconds > PERIOD_5_SEC)
    {
        webtext_json += ", \"temp_c_calib\":" + String(wdata.temp_c_calib, 2);
        webtext_json += ", \"temp_c\":" + String(wdata.temp_c);
        webtext_json += ", \"temp_f\":" + String(wdata.temp_f);
        webtext_json += ", \"pressure\":" + String(wdata.pressure);
        webtext_json += ", \"humidity\":" + String(wdata.humidity);

        webtext_json += ", \"wind_peak\":" + String(wdata.wind_peak);
        webtext_json += ", \"wind_rt\":" + String(wdata.wind_rt);
        webtext_json += ", \"wind_avg\":" + String(wdata.wind_avg);
        webtext_json += ", \"wind_dir_rt\":" + String(wdata.wind_dir_rt);
        webtext_json += ", \"wind_dir_avg\":" + String(wdata.wind_dir_avg);

        webtext_json += ", \"rain_calib\":" + String(wdata.rain_calib, 4); // More decimal places
        webtext_json += ", \"rain_rate\":" + String(wdata.rain_rate);
        webtext_json += ", \"rain_event\":" + String(wdata.rain_event);
        webtext_json += ", \"rain_event_cnt\":" + String(wdata.rain_event_cnt);
        webtext_json += ", \"rain_total\":" + String(wdata.rain_total);
    }
    webtext_json += " }";

    xSemaphoreGive(webtext_semaphore);
}

void handleRoot(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        last_request_sec = wdata.seconds;
        request->send(200, "text/html", webtext_root);
        xSemaphoreGive(webtext_semaphore);
    }
    else
        request->send(503, "text/html", "Resource busy, please retry.");
}

void handleJson(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        last_request_sec = wdata.seconds;
        request->send(200, "application/json", webtext_json);
        xSemaphoreGive(webtext_semaphore);
    }
    else
    {
        // For json, instead of the error message, return the station id only. We also log this error.
        wdata.error |= ERROR_SEM_2;
        request->send(503, "application/json", "{ \"id\":\"" + wdata.id + "\" }");
    }
}

template<class T> T parse(String value, char **p_next);
template<> inline uint32_t parse<uint32_t>(String value, char **p_next) { return strtoul(value.c_str(), p_next, 0); }
template<> inline float parse<float>(String value, char **p_next) { return strtof(value.c_str(), p_next); }
template<> inline String parse<String>(String value, char **p_next)
{
    value.trim();
    value.replace("\"", "'"); // Disallow the quotation character to ensure valid JSON output when printed
    return value;
}

// Parses the GET method ?name=value argument and returns false if the key or its value are not valid
// When the key name is matched, and the value is correct, it updates the wdata reference variable and its NV value
template <class T>
static bool get_parse_value(AsyncWebServerRequest *request, String key_name, T& dest)
{
    String value = request->arg(key_name);
    if (value.length())
    {
        char *p_next;
        T n = parse<T>(value, &p_next);

        // Check for validity of int and float types since we read them using strto* functions
        bool is_string = sizeof(T) == sizeof(String); // Little trick to tell String type apart without having the RTTI support
        if (is_string || ((p_next != value.c_str()) && (*p_next == 0) && (errno != ERANGE)))
        {
            dest = n; // Set the wdata.<key_name> member
            pref_set(key_name.c_str(), n); // Set the new value into the NV variable
            request->send(200, "text/html", "OK " + String(n));
            return true;
        }
    }
    return false;
}

// Set a variable from the client side. The key/value pairs are passed using an HTTP GET method.
void handleSet(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        last_request_sec = wdata.seconds;
        // Successfully updating a variable should respond with "OK" + the new value
        bool ok = false;
        ok |= get_parse_value(request, "id", wdata.id);
        ok |= get_parse_value(request, "tag", wdata.tag);
        ok |= get_parse_value(request, "wind_calib", wdata.wind_calib);
        ok |= get_parse_value(request, "rain_calib", wdata.rain_calib);
        ok |= get_parse_value(request, "rain_event", wdata.rain_event);
        ok |= get_parse_value(request, "rain_event_max", wdata.rain_event_max);
        ok |= get_parse_value(request, "rain_event_cnt", wdata.rain_event_cnt);
        ok |= get_parse_value(request, "rain_total", wdata.rain_total);
        ok |= get_parse_value(request, "error", wdata.error);
        ok |= get_parse_value(request, "temp_c_calib", wdata.temp_c_calib);
        if (!ok)
            request->send(400, "text/html", "?");

        xSemaphoreGive(webtext_semaphore);
    }
    else
        request->send(503, "text/html", "Resource busy, please retry.");
}

const char* uploadHtml = " \
<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script> \
<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'> \
 <input type='file' name='update'> \
  <input type='submit' value='Update'> \
 </form> \
<div id='prg'>Progress: 0%</div> \
<script> \
 $('form').submit(function(e) \
 { \
  e.preventDefault(); \
  var form = $('#upload_form')[0]; \
  var data = new FormData(form); \
  $.ajax({ \
   url: '/flash', \
   type: 'POST', \
   data: data, \
   contentType: false, \
   processData:false, \
   xhr: function() \
   { \
    var xhr = new window.XMLHttpRequest(); \
    xhr.upload.addEventListener('progress', function(evt) \
    { \
     if (evt.lengthComputable) \
     { \
      var per = evt.loaded / evt.total; \
      $('#prg').html('progress: ' + Math.round(per*100) + '%'); \
     } \
    }, false); \
    return xhr; \
   }, \
   success:function(d, s) \
   { \
       console.log('success!') \
   }, \
   error: function (a, b, c) { } \
  }); \
 }); \
</script> \
";

void setup_ota()
{
    server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", uploadHtml);
        response->addHeader("Connection", "close");
        request->send(response);
    });
    server.on("/flash", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        response->addHeader("Connection", "close");
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
    {
        // Serial.printf("Uploading: index=%d len=%d final=%d\n", index, len, final);
        if (index == 0)
        {
            Serial.printf("Uploading: %s\n", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) // start with max available size
                Update.printError(Serial);
        }
        if (!Update.hasError())
        {
            if (Update.write(data, len) != len) // flashing firmware to ESP
                Update.printError(Serial);
        }
        if (final)
        {
            if (Update.end(true)) // true to set the size to the current progress
            {
                Serial.println("Flash OK, rebooting...\n");
                ota_restart_pending = true;
            }
            else
                Update.printError(Serial);
        }
    });
}

// Maximum number of 500ms retries before giving up on a connection attempt (30 seconds)
#define WIFI_CONNECT_RETRIES  60

// If WiFi appears connected but no request has been served for this many seconds, force a
// reconnect cycle as a watchdog against the ESP32 WiFi stack going unresponsive
#define WIFI_WATCHDOG_SEC       (60 * 60)

// Suppress the watchdog if a request was served within this many seconds
#define WIFI_WATCHDOG_QUIET_SEC (60 * 15)

static uint32_t wifi_watchdog_counter = 0;

// Attempt to connect to WiFi. Returns true if connected, false if timed out.
static bool wifi_connect()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    IPAddress ip(192,168,1,GET_IP4);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);
    WiFi.config(ip, gateway, subnet);
    WiFi.begin(ssid, password);
    wifi_mac = WiFi.macAddress();

    // Wait for connection with a timeout
    for (int attempt = 0; attempt < WIFI_CONNECT_RETRIES; attempt++)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("\nConnected to %s\nIP address: ", ssid);
            Serial.println(WiFi.localIP());
            reconnects++;
            wifi_watchdog_counter = 0;
            return true;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nWiFi connection timed out");
    return false;
}

void setup_wifi()
{
    // Initial connection: keep trying until successful (blocking at boot is acceptable)
    while (!wifi_connect())
    {
        Serial.println("Retrying WiFi...");
        delay(5000);
    }
}

void setup_webserver()
{
    webtext_semaphore = xSemaphoreCreateMutex();
    xSemaphoreGive(webtext_semaphore);

    webserver_set_response();
    server.on("/", handleRoot);
    server.on("/json", handleJson);
    server.on("/set", handleSet);
    setup_ota();
    server.begin();
}

void wifi_check_loop()
{
    if (ota_restart_pending)
    {
        delay(500); // Allow the async server to send the "OK" response
        ESP.restart();
    }
    delay(1000);
    wifi_watchdog_counter++;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi disconnected! Reconnecting...");
        wifi_connect();
    }
    else if ((wifi_watchdog_counter >= WIFI_WATCHDOG_SEC) &&
             ((wdata.seconds - last_request_sec) >= WIFI_WATCHDOG_QUIET_SEC))
    {
        // Periodic forced reconnect as a watchdog against the ESP32 WiFi stack going unresponsive
        // Skip if a request was successfully served recently — that proves WiFi is working
        Serial.println("WiFi watchdog: forcing reconnect...");
        wifi_connect();
    }
}
