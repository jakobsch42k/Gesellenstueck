#include "actuators.h"
#include "pins.h"

static const int VALVE_PINS[5] = {
    VALVE_PIN_1, VALVE_PIN_2, VALVE_PIN_3, VALVE_PIN_4, VALVE_PIN_5
};

static const unsigned long BLINK_INTERVAL_MS = 500;

void actuators_init() {
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
    for (int i = 0; i < 5; i++) {
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

void roof_open(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_A_IN1, HIGH);
    digitalWrite(MOTOR_A_IN2, LOW);
    analogWrite(MOTOR_A_PWM, pwm);
    Serial.println("[actuators] roof opening");
}

void roof_close(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, HIGH);
    analogWrite(MOTOR_A_PWM, pwm);
    Serial.println("[actuators] roof closing");
}

void roof_stop() {
    analogWrite(MOTOR_A_PWM, 0);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    // Leave STBY HIGH — pump may still be running on channel B
    Serial.println("[actuators] roof stopped");
}

// ── Pump ─────────────────────────────────────────────────────────────────────

void pump_on(int pwm) {
    digitalWrite(MOTOR_STBY, HIGH);
    digitalWrite(MOTOR_B_IN1, HIGH);
    digitalWrite(MOTOR_B_IN2, LOW);
    analogWrite(MOTOR_B_PWM, pwm);
    Serial.println("[actuators] pump ON");
}

void pump_off() {
    analogWrite(MOTOR_B_PWM, 0);
    digitalWrite(MOTOR_B_IN1, LOW);
    digitalWrite(MOTOR_B_IN2, LOW);
    Serial.println("[actuators] pump OFF");
}

// ── Solenoid valves ───────────────────────────────────────────────────────────

void valve_open(int index) {
    if (index < 0 || index > 4) return;
    digitalWrite(VALVE_PINS[index], HIGH);
    Serial.println("[actuators] valve " + String(index + 1) + " open");
}

void valve_close(int index) {
    if (index < 0 || index > 4) return;
    digitalWrite(VALVE_PINS[index], LOW);
    Serial.println("[actuators] valve " + String(index + 1) + " closed");
}

void valve_closeAll() {
    for (int i = 0; i < 5; i++) {
        digitalWrite(VALVE_PINS[i], LOW);
    }
    Serial.println("[actuators] all valves closed");
}

// ── Grow light ────────────────────────────────────────────────────────────────

void led_grow_setPWM(int pwm) {
    pwm = constrain(pwm, 0, 255);
    analogWrite(LED_GROW_PWM, pwm);
}

// ── Warning LED ───────────────────────────────────────────────────────────────

void led_warn_on() {
    digitalWrite(LED_WARN, HIGH);
}

void led_warn_off() {
    digitalWrite(LED_WARN, LOW);
}

void led_warn_blink() {
    static unsigned long lastToggle = 0;
    static bool state = false;
    if (millis() - lastToggle >= BLINK_INTERVAL_MS) {
        state = !state;
        digitalWrite(LED_WARN, state ? HIGH : LOW);
        lastToggle = millis();
    }
}

// ── Emergency stop ────────────────────────────────────────────────────────────

void emergency_stop_all() {
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

    Serial.println("[actuators] EMERGENCY STOP — all actuators off");
}
