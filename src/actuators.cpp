#include "actuators.h"
#include "pins.h"

static const int VALVE_PINS[NUM_BEDS] = {
    VALVE_PIN_1, VALVE_PIN_2, VALVE_PIN_3, VALVE_PIN_4, VALVE_PIN_5
};

static const unsigned long BLINK_INTERVAL_MS = 500;

void Actuators::init() {
    // Motor driver standby
    pinMode(MOTOR_STBY, OUTPUT);
    digitalWrite(MOTOR_STBY, LOW); // start in standby

    // Roof motor (channel A)
    pinMode(MOTOR_A_IN1, OUTPUT);
    pinMode(MOTOR_A_IN2, OUTPUT);
    pinMode(MOTOR_A_PWM, OUTPUT);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    analogWrite(MOTOR_A_PWM, 0);

    // Pump (channel B)
    pinMode(MOTOR_B_IN1, OUTPUT);
    pinMode(MOTOR_B_IN2, OUTPUT);
    pinMode(MOTOR_B_PWM, OUTPUT);
    digitalWrite(MOTOR_B_IN1, LOW);
    digitalWrite(MOTOR_B_IN2, LOW);
    analogWrite(MOTOR_B_PWM, 0);

    // Solenoid valves — all closed on boot
    for (int i = 0; i < NUM_BEDS; i++) {
        pinMode(VALVE_PINS[i], OUTPUT);
        digitalWrite(VALVE_PINS[i], LOW);
    }

    // Grow light
    pinMode(LED_GROW_PWM, OUTPUT);
    analogWrite(LED_GROW_PWM, 0);

    // Warning LED
    pinMode(LED_WARN, OUTPUT);
    digitalWrite(LED_WARN, LOW);

    Serial.println("[actuators] init OK");
}

// ── Roof motor ────────────────────────────────────────────────────────────────

void Actuators::roof_open(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_A_IN1, HIGH);
    digitalWrite(MOTOR_A_IN2, LOW);
    analogWrite(MOTOR_A_PWM, pwm);
    if (roofDirection != 1) Serial.println("[actuators] roof opening");
    roofDirection = 1;
}

void Actuators::roof_close(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, HIGH);
    analogWrite(MOTOR_A_PWM, pwm);
    if (roofDirection != -1) Serial.println("[actuators] roof closing");
    roofDirection = -1;
}

void Actuators::roof_stop() {
    analogWrite(MOTOR_A_PWM, 0);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    // Leave STBY HIGH — pump may still be running on channel B
    if (roofDirection != 0) Serial.println("[actuators] roof stopped");
    roofDirection = 0;
}

// ── Pump ─────────────────────────────────────────────────────────────────────

void Actuators::pump_on(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_B_IN1, LOW);
    digitalWrite(MOTOR_B_IN2, HIGH);
    analogWrite(MOTOR_B_PWM, pwm);
    if (!pumpRunning) Serial.println("[actuators] pump ON");
    pumpRunning = true;
}

void Actuators::pump_off() {
    analogWrite(MOTOR_B_PWM, 0);
    digitalWrite(MOTOR_B_IN1, LOW);
    digitalWrite(MOTOR_B_IN2, LOW);
    if (pumpRunning) Serial.println("[actuators] pump OFF");
    pumpRunning = false;
}

// ── Solenoid valves ───────────────────────────────────────────────────────────

void Actuators::valve_open(int index) {
    if (index < 0 || index >= NUM_BEDS) return;
    digitalWrite(VALVE_PINS[index], HIGH);
    if (!valveIsOpen[index]) Serial.println("[actuators] valve " + String(index + 1) + " open");
    valveIsOpen[index] = true;
}

void Actuators::valve_close(int index) {
    if (index < 0 || index >= NUM_BEDS) return;
    digitalWrite(VALVE_PINS[index], LOW);
    if (valveIsOpen[index]) Serial.println("[actuators] valve " + String(index + 1) + " closed");
    valveIsOpen[index] = false;
}

void Actuators::valve_closeAll() {
    bool anyWasOpen = false;
    for (int i = 0; i < NUM_BEDS; i++) {
        digitalWrite(VALVE_PINS[i], LOW);
        anyWasOpen |= valveIsOpen[i];
        valveIsOpen[i] = false;
    }
    if (anyWasOpen) Serial.println("[actuators] all valves closed");
}

// ── Grow light ────────────────────────────────────────────────────────────────

void Actuators::led_grow_setPWM(int pwm) {
    pwm = constrain(pwm, 0, 255);
    analogWrite(LED_GROW_PWM, pwm);
}

// ── Warning LED ───────────────────────────────────────────────────────────────

void Actuators::led_warn_on() {
    digitalWrite(LED_WARN, HIGH);
}

void Actuators::led_warn_off() {
    digitalWrite(LED_WARN, LOW);
}

void Actuators::led_warn_blink() {
    if (millis() - blinkLastToggle >= BLINK_INTERVAL_MS) {
        blinkState = !blinkState;
        digitalWrite(LED_WARN, blinkState ? HIGH : LOW);
        blinkLastToggle = millis();
    }
}

// ── Emergency stop ────────────────────────────────────────────────────────────

void Actuators::emergency_stop_all() {
    // Called every loop pass while EMERGENCY_STOP is latched — log only on
    // the transition from active to stopped, not on every re-assert.
    bool anythingActive = pumpRunning || roofDirection != 0;
    for (int i = 0; i < NUM_BEDS; i++) anythingActive |= valveIsOpen[i];

    // Motor driver into standby — cuts both channels at hardware level
    digitalWrite(MOTOR_STBY, LOW);
    analogWrite(MOTOR_A_PWM, 0);
    analogWrite(MOTOR_B_PWM, 0);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    digitalWrite(MOTOR_B_IN1, LOW);
    digitalWrite(MOTOR_B_IN2, LOW);

    valve_closeAll();
    analogWrite(LED_GROW_PWM, 0);
    led_warn_on();

    pumpRunning   = false;
    roofDirection = 0;
    if (anythingActive) Serial.println("[actuators] EMERGENCY STOP — all actuators off");
}
