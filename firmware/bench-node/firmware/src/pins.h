// The pin file: the one place the bench node's GPIOs are assigned (ESP32-S3-DevKitC-1 N16R8).
// Signal names are exactly those of the hardware drawings and the Hardware Bible. Wiring and electrical levels are
// hardware facts: where this file disagrees with the Bible, the Bible is right.
#pragma once

constexpr int RADAR_to_S3 = 4;     // presence sensor TX -> S3 RX (UART)
constexpr int S3_to_RADAR = 5;     // S3 TX -> presence sensor RX (UART)
constexpr int RADAR_OUT = 6;       // presence sensor OUT pin
constexpr int KEY_ON = 9;
constexpr int KEY_OVERRIDE = 10;
constexpr int KEY_FN = 11;
constexpr int LED_ON = 12;
constexpr int LED_OVERRIDE = 13;
constexpr int LED_FN = 14;
constexpr int BUZZER = 15;         // buzzer drive; the circuit is in the Hardware Bible
constexpr int I2C_SCL = 16;
constexpr int I2C_SDA = 17;
constexpr int BAR_LIVE = 18;       // low = bar live (the PC817 in the mains box pulls it down)
