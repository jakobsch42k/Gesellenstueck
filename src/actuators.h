#pragma once

#include "shared.h"

void actuators_init();

// Roof motor (TB6612FNG channel A)
void roof_open(int pwm = 120);
void roof_close(int pwm = 70);
void roof_stop();

// Pump (TB6612FNG channel B)
void pump_on(int pwm = 200);
void pump_off();

// Solenoid valves (index 0–4)
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
