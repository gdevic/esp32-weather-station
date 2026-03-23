#include "main.h"

#include <DHT22.h>

// SDA, or almost any other I/O pin. SDA is GPIO 21 on ESP32-WROOM-32U board
#define pinDATA SDA

DHT22 dht22(pinDATA);

bool setup_dht22()
{   
    delay(1000); // Wait a second after the initialization
    float sample = dht22.getTemperature();
    if (dht22.getLastError() != 0)
        return false;
    Serial.println("Using DHT22");
    delay(1000);
    return true;    
}

void read_dht22()
{
    // Reading temperature and humidity
    float temperature = dht22.getTemperature();
    float humidity = dht22.getHumidity();

    // Check if any reads failed and exit early (to try again)
    if (dht22.getLastError() != dht22.OK)
    {
        wdata.error |= ERROR_DHT_READ;
        Serial.println("Failed to read from DHT sensor!");
        Serial.println(dht22.getLastError());
        return;
    }

    wdata.temp_c = temperature;
    wdata.temp_c += wdata.temp_c_calib; // Apply calibration value
    wdata.temp_f = wdata.temp_c * 9.0 / 5.0 + 32.0;
    wdata.pressure = 0;
    wdata.humidity = humidity;

#ifdef TEST
    // Print the results
    Serial.print("DHT22: Humidity: ");
    Serial.print(humidity);
    Serial.print("%  Temperature: ");
    Serial.print(temperature);
    Serial.println("°C");
#endif
}
