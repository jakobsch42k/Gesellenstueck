#pragma once

#include "shared.h"
#include <Adafruit_BME280.h>
#include <BH1750.h>

// All sensor inputs: BME280, BH1750, soil ADC, water level switches, reed
// contact. Owned by SystemController; the instance lives for the full program
// duration. Writes LiveData; all other modules read it.
class Sensors {
public:
    void init(LiveData& data, ErrorFlags& err, const Config& cfg);
    void update(LiveData& data, ErrorFlags& err, const Config& cfg);
    void setTime(LiveData& data, unsigned long unixTimestamp);

private:
    Adafruit_BME280 bme;
    BH1750          lightMeter;

    // Lux EMA smoothing
    bool          luxEmaSeeded  = false;
    unsigned long lastLuxSample = 0;

    // Time tracking
    unsigned long lastTimeUpdate = 0;
};
