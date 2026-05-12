#include "sensors.h"
#include "constants.h"
#include "pins.h"
// #include <Wire.h>
// #include <Adafruit_BME280.h>
// #include <BH1750.h>

// static Adafruit_BME280 bme;
// static BH1750          lightMeter;

// Lux ring buffer for smoothing
// static float          luxBuf[LUX_SMOOTH_WINDOW_S];
// static int            luxBufIndex  = 0;
// static bool           luxBufFilled = false;
// static unsigned long  lastLuxSample = 0;

// Soil sensor ADC pins in index order
static const int SOIL_PINS[5] = {
    SOIL_PIN_1, SOIL_PIN_2, SOIL_PIN_3, SOIL_PIN_4, SOIL_PIN_5
};

// Time tracking
static unsigned long lastTimeUpdate = 0;

// // ── Lux smoothing ─────────────────────────────────────────────────────────────

// static void luxBuf_push(float value) {
//     luxBuf[luxBufIndex] = value;
//     luxBufIndex = (luxBufIndex + 1) % LUX_SMOOTH_WINDOW_S;
//     if (luxBufIndex == 0) luxBufFilled = true;
// }

// static float luxBuf_average() {
//     int count = luxBufFilled ? LUX_SMOOTH_WINDOW_S : luxBufIndex;
//     if (count == 0) return 0.0f;
//     float sum = 0.0f;
//     for (int i = 0; i < count; i++) sum += luxBuf[i];
//     return sum / count;
// }

// ── Init ──────────────────────────────────────────────────────────────────────

void sensors_init(LiveData& data, ErrorFlags& err, const Config& cfg) {
    // Wire.begin(PIN_SDA, PIN_SCL);

    // // BME280
    // if (!bme.begin(0x76)) {
    //     // Try alternate address
    //     if (!bme.begin(0x77)) {
    //         err.ERR_SENSOR_BME = true;
    //         err.lastErrorMessage = "BME280 not found on I2C";
    //         Serial.println("[sensors] ERROR: BME280 not found");
    //     }
    // }
    // if (!err.ERR_SENSOR_BME) {
    //     Serial.println("[sensors] BME280 OK");
    // }

    // // BH1750
    // if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    //     err.ERR_SENSOR_BH = true;
    //     err.lastErrorMessage = "BH1750 not found on I2C";
    //     Serial.println("[sensors] ERROR: BH1750 not found");
    // } else {
    //     Serial.println("[sensors] BH1750 OK");
    // }

    // // Digital inputs — external pull-down resistors on PCB, no internal pull needed
    // pinMode(WATER_LOW_PIN,      INPUT);
    // pinMode(WATER_CRITICAL_PIN, INPUT);
    // pinMode(REED_PIN,           INPUT);

    // Safe defaults so no error flags or shutdowns trigger
    data.waterLow      = true;
    data.waterCritical = true;
    data.roofClosed    = false;

    for (int i = 0; i < 5; i++) {
        data.soilRaw[i]  = 0;
        data.soilPerc[i] = 0;
    }

    // Flag sensors as absent so modules use their open-loop fallbacks
    err.ERR_SENSOR_BME = true;
    err.ERR_SENSOR_BH  = true;

    // Do an initial read to populate LiveData before first loop
    sensors_update(data, err, cfg);

    lastTimeUpdate = millis();
    Serial.println("[sensors] init OK (no hardware)");
}

// ── Update ────────────────────────────────────────────────────────────────────

void sensors_update(LiveData& data, ErrorFlags& err, const Config& cfg) {

    // ── Advance timeOfDay ────────────────────────────────────────────────────
    unsigned long now = millis();
    unsigned long elapsed = (now - lastTimeUpdate) / 1000; // whole seconds only
    if (elapsed > 0) {
        data.timeOfDay = (data.timeOfDay + elapsed) % 86400;
        lastTimeUpdate += elapsed * 1000;
    }

    // // ── BME280 ───────────────────────────────────────────────────────────────
    // if (!err.ERR_SENSOR_BME) {
    //     float t = bme.readTemperature();
    //     float h = bme.readHumidity();
    //     // NaN indicates a read failure — keep last good value
    //     if (!isnan(t) && !isnan(h)) {
    //         data.tempC   = t;
    //         data.humPerc = h;
    //     } else {
    //         Serial.println("[sensors] WARNING: BME280 read returned NaN");
    //     }
    // }

    // // ── BH1750 ───────────────────────────────────────────────────────────────
    // if (!err.ERR_SENSOR_BH) {
    //     float lux = lightMeter.readLightLevel();
    //     if (lux >= 0) {
    //         data.lux = lux;

    //         // Add one sample per second to the smoothing buffer
    //         if (millis() - lastLuxSample >= 1000) {
    //             luxBuf_push(lux);
    //             data.luxSmoothed = luxBuf_average();
    //             lastLuxSample = millis();
    //         }
    //     } else {
    //         Serial.println("[sensors] WARNING: BH1750 read failed");
    //     }
    // }

    // ── Soil moisture sensors ────────────────────────────────────────────────
    for (int i = 0; i < 5; i++) {
        long sum = 0;
        for (int s = 0; s < SOIL_ADC_SAMPLES; s++) {
            sum += analogRead(SOIL_PINS[i]);
        }
        int raw = (int)(sum / SOIL_ADC_SAMPLES);
        data.soilRaw[i] = raw;

        if (raw < SOIL_ADC_MIN || raw > SOIL_ADC_MAX) {
            err.ERR_SOIL[i] = true;
            Serial.println("[sensors] WARNING: soil sensor " + String(i + 1) +
                           " out of range (" + String(raw) + ")");
        } else {
            err.ERR_SOIL[i] = false;
            int perc = map(raw, cfg.soilDryValue, cfg.soilWetValue, 0, 100);
            data.soilPerc[i] = constrain(perc, 0, 100);
        }
    }

    // // ── Digital inputs ───────────────────────────────────────────────────────
    // data.waterLow      = digitalRead(WATER_LOW_PIN);      // HIGH = OK
    // data.waterCritical = digitalRead(WATER_CRITICAL_PIN); // HIGH = OK
    // data.roofClosed    = digitalRead(REED_PIN);           // HIGH = closed
}

// ── Time setter (called by webBackend /setTime) ───────────────────────────────

void sensors_setTime(LiveData& data, unsigned long unixTimestamp) {
    // Extract seconds since midnight from Unix timestamp (UTC)
    data.timeOfDay = unixTimestamp % 86400;
    lastTimeUpdate = millis();
    Serial.println("[sensors] timeOfDay set to " + String(data.timeOfDay) + "s");
}
