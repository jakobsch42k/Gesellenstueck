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
    // Time-based debounce for a noisy digital input. The committed value only
    // changes after a new raw read holds steady for stableMs; transient spikes
    // (motor/valve EMI on unshielded switch wiring) are rejected.
    struct DebouncedInput {
        bool          committed  = false;
        bool          candidate  = false;
        bool          seeded     = false;
        unsigned long changedAt  = 0;

        // Seed both states to a known-safe value before the first read.
        void seed(bool value) {
            committed = candidate = value;
            seeded    = true;
        }

        bool update(bool raw, unsigned long now, unsigned long stableMs) {
            if (!seeded) { seed(raw); return committed; }
            if (raw != candidate) {          // new level — restart stability timer
                candidate = raw;
                changedAt = now;
            } else if (raw != committed && now - changedAt >= stableMs) {
                committed = raw;             // held long enough — commit
            }
            return committed;
        }
    };

    DebouncedInput waterLowInput;
    DebouncedInput waterCriticalInput;
    DebouncedInput reedInput;

    Adafruit_BME280 bme;
    BH1750          lightMeter;

    // Runtime I2C-fault tracking. Faults are only known at boot otherwise; these
    // debounce a disconnect/reconnect at runtime (hysteresis + throttled re-init).
    uint8_t       bmeFail = 0, bmeOk = 0;
    uint8_t       bhFail  = 0, bhOk  = 0;
    uint8_t       bmeAddr = 0;          // I2C address BME280 was found at (0 = none)

    bool beginBme();   // (re)initialise BME280; true on success, records bmeAddr
    bool beginBh();    // (re)initialise BH1750; true on success

    // True if a device ACKs at this I2C address. Detects a runtime disconnect
    // reliably — the BME280 library returns garbage (not NaN) on a dead bus.
    static bool i2cPresent(uint8_t addr);

    // Lux EMA smoothing
    bool          luxEmaSeeded  = false;
    unsigned long lastLuxSample = 0;

    // Time tracking
    unsigned long lastTimeUpdate = 0;
};
