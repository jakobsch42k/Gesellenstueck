#include "display.h"
#include "constants.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

// Pad or truncate a string to exactly 'width' characters
static String padTo(const String& s, int width) {
    String result = s;
    while (result.length() < (unsigned)width) result += ' ';
    if (result.length() > (unsigned)width) result = result.substring(0, width);
    return result;
}

void display_init() {
    Wire.begin(PIN_SDA, PIN_SCL);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Hochbeet v1.0   ");
    lcd.setCursor(0, 1);
    lcd.print("Initialisiere...");
    Serial.println("[display] init OK");
}

void display_update(const LiveData& data, const ErrorFlags& err) {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate < DISPLAY_UPDATE_MS) return;
    lastUpdate = millis();

    // Line 1: temperature and humidity
    String line1 = "T:" + String(data.tempC, 1) + "C  H:" + String((int)data.humPerc) + "%";
    lcd.setCursor(0, 0);
    lcd.print(padTo(line1, 16));

    // Line 2: "Wasser XX   MODE" — water left, system mode right, 16 chars total
    String water = (!data.waterCritical || err.ERR_WATER_CRITICAL) ? "Wasser KRIT"
                 : !data.waterLow                                   ? "Wasser LOW"
                                                                    : "Wasser OK";
    String mode  = err.EMERGENCY_STOP  ? "STOP"
                 : err.MAINTENANCE_MODE ? "Main"
                                        : "Auto";
    // Right-align mode in the remaining columns
    String line2 = padTo(water, 16 - (int)mode.length()) + mode;
    lcd.setCursor(0, 1);
    lcd.print(padTo(line2, 16));
}
