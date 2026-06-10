#pragma once

#include "shared.h"

// All GPIO outputs: roof motor, pump, solenoid valves, grow light, warning LED.
// Owned by SystemController; the instance lives for the full program duration,
// so raw pointers/references handed to control modules stay valid.
class Actuators {
public:
    void init();

    // Roof motor (TB6612FNG channel A) — PWM comes from Config at call sites
    void roof_open(int pwm);
    void roof_close(int pwm);
    void roof_stop();

    // Pump (TB6612FNG channel B)
    void pump_on(int pwm);
    void pump_off();

    // Solenoid valves (index 0–NUM_BEDS-1)
    void valve_open(int index);
    void valve_close(int index);
    void valve_closeAll();

    // Grow light strip (PWM 0–255)
    void led_grow_setPWM(int pwm);

    // Warning LED
    void led_warn_on();
    void led_warn_off();
    void led_warn_blink(); // Non-blocking, call every loop()

    // Cuts all actuators immediately
    void emergency_stop_all();

private:
    // Logical state, used only to suppress repeated log lines. Critical-fault
    // paths call pump_off()/valve_closeAll() every loop pass (idempotent
    // hardware writes by design) — without this the serial monitor floods.
    // Hardware writes still happen on every call as a safe re-assert.
    bool pumpRunning              = false;
    bool valveIsOpen[NUM_BEDS]    = {false};
    int  roofDirection            = 0;   // 0 = stopped, 1 = opening, -1 = closing
    unsigned long blinkLastToggle = 0;
    bool blinkState               = false;
};
