#pragma once

// ── I2C Bus ──────────────────────────────────────────────────────────────────
#define PIN_SDA              8
#define PIN_SCL              9
// Devices on bus: BME280, BH1750, LCD 16x2 (address 0x27)

// ── Soil Moisture Sensors (ADC, 0–4095) ─────────────────────────────────────
#define SOIL_PIN_1           4   // BM1 – Bed 1
#define SOIL_PIN_2           5   // BM2 – Bed 2
#define SOIL_PIN_3           6   // BM3 – Bed 3
#define SOIL_PIN_4           7   // BM4 – Bed 4
#define SOIL_PIN_5          10   // BM5 – Bed 5

// ── Water Level Sensors (float switch, ext pull-down, INPUT) ─────────────────
// HIGH = float closed = water too low, LOW = float open = sufficient
#define WATER_LOW_PIN        2   // BG1 – warning level
#define WATER_CRITICAL_PIN   1   // BG2 – critical, locks pump

// ── Reed Contact Roof End-Position (INPUT_PULLUP) ────────────────────────────
// HIGH = roof closed, LOW = roof not in end position
#define REED_PIN             3   // BG3

// ── TB6612FNG Motor Driver ───────────────────────────────────────────────────
// Channel A → roof motor (MA1)
#define MOTOR_STBY          17   // STBY – LOW = entire driver in standby
#define MOTOR_A_PWM         11   // PWMA
#define MOTOR_A_IN1         12   // AI1
#define MOTOR_A_IN2         13   // AI2

// Channel B → pump (MA2)
#define MOTOR_B_PWM         14   // PWMB
#define MOTOR_B_IN1         15   // BI1
#define MOTOR_B_IN2         16   // BI2

// ── Solenoid Valves (HIGH = open, LOW = closed) ──────────────────────────────
#define VALVE_PIN_1         46   // MB1 – Bed 1
#define VALVE_PIN_2         47   // MB2 – Bed 2
#define VALVE_PIN_3         48   // MB3 – Bed 3
#define VALVE_PIN_4         39   // MB4 – Bed 4
#define VALVE_PIN_5         45   // MB5 – Bed 5

// ── Outputs ──────────────────────────────────────────────────────────────────
#define LED_GROW_PWM        21   // EA1 – grow light strip (PWM 0–255)
#define LED_WARN            18   // PF1 – warning LED (HIGH = on)
