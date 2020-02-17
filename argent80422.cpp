// Argent Data Systems weather assembly 80422
// https://www.argentdata.com/catalog/product_info.php?products_id=145
// Datasheet: https://www.sparkfun.com/datasheets/Sensors/Weather/Weather%20Sensor%20Assembly..pdf
#include "main.h"

#define ANEMOMETER_PIN  33
#define WINDVANE_PIN    34
#define RAINGAUGE_PIN   35

Gauge anem = {};
Gauge rain = {};

Gauge::Gauge()
{
    lock = portMUX_INITIALIZER_UNLOCKED;
}

// Use isr wrapper since Arduino interrupt service function cannot be a class member due to implicit "this" pointer
void IRAM_ATTR Gauge::isr()
{
    portENTER_CRITICAL_ISR(&lock);

    uint32_t current_time = micros();
    uint32_t delta_time = current_time - last_time;

    // Ignore interrupts that are closer than 1 ms apart
    // A reed switch would have to open and close 1000 times per second to reach this rate
    if (delta_time > 1000)
    {
        last_time = current_time;
        count++;
        count2++;
    }
    portEXIT_CRITICAL_ISR(&lock);
}

// The interrupt handling routine should have the IRAM_ATTR attribute, in order for the compiler to place the code in IRAM.
// Also, interrupt handling routines should only call functions also placed in IRAM.
void IRAM_ATTR anem_isr() { anem.isr(); }
void IRAM_ATTR rain_isr() { rain.isr(); }

int read_wind_dir_adc()
{
    // Multi-sample ADC input to filter out the noise
    int total = 0;
    for (int i = 0; i < 16; i++)
        total += analogRead(WINDVANE_PIN);
    total = total / 16;

    return total;
}

// Given the ADC sensor reading, returns the wind direction 0-15, N->E->S->W->
int wind_calc_dir(int adc)
{
    // ADC returns 0-4096 corresponding to 0V to 3.3V but it is not linear
    // https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/peripherals/adc.html

    // This table contains values that are measured on an actual wind vane that is in use
    // With a 10k Ohm on-board pullup
    //
    // Direction     Resistance  Voltage (mV)
    // (Degrees)     (Ohms)      measured   ADC range   avg.
    //     0     N   33k         2535       2966,2990   2978
    //     22.5      6.57k       1308       1424,1436   1430
    //     45    NE  8.2k        1487       1650,1664   1657
    //     67.5      891          273       143,143     143

    //     90    E   1k           303       175,178     176
    //     112.5     688          216       68,71       70
    //     135   SE  2.2k         596       536,545     540
    //     157.5     1.41k        408       306,307     307

    //     180   S   3.9k         921       939,945     942
    //     202.5     3.14k        785       774,775     775
    //     225   SW  16k         2028       2319,2337   2328
    //     247.5     14.12k      1929       2212,2217   2215

    //     270   W   120k        3051       3889,3912   3900
    //     292.5     42.12k      2669       3173,3176   3175
    //     315   NW  64.9k       2862       3485,3503   3494
    //     337.5     21.88k      2266       2633,2638   2636

    // Values are sorted and have corresponding direction index; arrays include end-point sentinels
    static int val[1 + 16 + 1] = { 0, 70, 143, 176, 307, 540, 775, 942,1430,1657,2215,2328,2636,2978,3175,3494,3900,4096 };
    static int dir[1 + 16 + 1] = { 5,  5,   3,   4,   7,   6,   9,   8,   1,   2,  11,  10,  15,   0,  13,  14,  12,  12 };

    for (int i = 0; i < 17; i++) // i indexes from 0 to 16, so that (i + 1) = 17, max index into arrays
    {
        if (adc < val[i + 1])
        {
            int d0 = adc - val[i];
            int d1 = val[i + 1] - adc;
            return (d0 < d1) ? dir[i] : dir[i + 1];
        }
    }
    return 0; // We should NEVER be here
}

void setup_wind_rain()
{
    // Anemometer (wind speed) interrupt
    pinMode(ANEMOMETER_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), anem_isr, RISING);

    // Rain gauge interrupt
    pinMode(RAINGAUGE_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RAINGAUGE_PIN), rain_isr, RISING);

    // Wind vane ADC
    pinMode(WINDVANE_PIN, INPUT);
    analogReadResolution(12); // This may be the default?
}
