#pragma once

#include "pins.h"

// ── WiFi Credentials ─────────────────────────────────────────────────────────
#define WIFI_SSID                  "Esp-32-Webserver"
#define WIFI_PASS                  "12345678"

// ── Config Defaults ──────────────────────────────────────────────────────────
#define DEFAULT_MOISTURE            40    // Target soil moisture per bed (%)
#define DEFAULT_TEMP_TARGET         25.0f // Target temperature (°C)
#define DEFAULT_TEMP_HYSTERESIS      1.0f // Hysteresis band (°C)
#define DEFAULT_LUX_TARGET         500    // Base lux setpoint

// ── Timing Defaults (all in milliseconds) ────────────────────────────────────
#define DEFAULT_ROOF_OPEN_MS       1000UL  // Motor run time to open roof
#define DEFAULT_ROOF_CLOSE_TIMEOUT 5000UL  // Max wait for reed contact when closing
#define DEFAULT_IRRIGATE_MS       10000UL  // Pump run time per irrigation cycle
#define DEFAULT_IRRIGATE_PAUSE   600000UL  // Diffusion pause between cycles

// ── ADC Calibration ──────────────────────────────────────────────────────────
#define DEFAULT_SOIL_DRY           3500   // ADC count for completely dry sensor
#define DEFAULT_SOIL_WET            800   // ADC count for saturated sensor
#define SOIL_ADC_MIN                 50   // Below this → sensor fault
#define SOIL_ADC_MAX               4000   // Above this → sensor fault
#define SOIL_ADC_SAMPLES             10   // Averaged readings per sensor per cycle

// ── Light Management ─────────────────────────────────────────────────────────
#define LUX_SMOOTH_WINDOW_S          60   // Smoothing window for lux average (seconds)
#define LUX_HYSTERESIS               20   // Dead-band for P-controller (lux)

// ── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_UPDATE_MS          2000UL // LCD refresh interval

// ── Config File ──────────────────────────────────────────────────────────────
#define CONFIG_VERSION                1
#define CONFIG_PATH            "/config.json"
#define CONFIG_TMP_PATH        "/config.tmp"
#define CONFIG_BAK1_PATH       "/config.bak1"
#define CONFIG_BAK2_PATH       "/config.bak2"
#define CONFIG_BAK3_PATH       "/config.bak3"
#define PLANTS_PATH            "/plants.json"
