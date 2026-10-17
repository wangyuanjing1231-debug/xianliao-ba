#pragma once
#include <Arduino.h>

#define LED_DATA_PIN 40
#define LED_CLK_PIN  39

class Apa102Led {
public:
  void begin() {
    pinMode(LED_DATA_PIN, OUTPUT);
    pinMode(LED_CLK_PIN, OUTPUT);
    digitalWrite(LED_DATA_PIN, LOW);
    digitalWrite(LED_CLK_PIN, LOW);
    setColor(0, 0, 0, 0);
  }

  void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 15) {
    auto sendByte = [](uint8_t val) {
      for (int i = 7; i >= 0; i--) {
        digitalWrite(LED_DATA_PIN, (val >> i) & 1);
        digitalWrite(LED_CLK_PIN, HIGH);
        delayMicroseconds(1);
        digitalWrite(LED_CLK_PIN, LOW);
        delayMicroseconds(1);
      }
    };

    // Start frame: 32 zero bits
    for (int i = 0; i < 4; i++) sendByte(0x00);

    // LED frame: 111[5-bit brightness] [B] [G] [R]
    sendByte(0xE0 | (brightness & 0x1F));
    sendByte(b);
    sendByte(g);
    sendByte(r);

    // End frame: 32 one bits
    for (int i = 0; i < 4; i++) sendByte(0xFF);
  }
};

extern Apa102Led petLed;
