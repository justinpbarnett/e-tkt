#include "Light.h"

#include <Arduino.h>

#include "analogWrite.h"

// PWM value at LIGHT_FULL. Deliberately half of the ESP32 analogWrite shim's
// 8-bit range: the LEDs sit on the 3V3 rail with no series resistor sized for
// full duty, and this is the brightness every E-TKT has shipped with. It is
// private to this file on purpose -- see the class comment in Light.h.
static const int LIGHT_PWM_FULL = 128;

Light::Light(uint8_t pin) { this->pin = pin; }

void Light::initialize() {
  pinMode(this->pin, OUTPUT);
  this->off();
}

int Light::level(float brightness) {
  if (brightness <= 0.0f) {
    return 0;
  }
  if (brightness >= 1.0f) {
    return LIGHT_PWM_FULL;
  }
  return (int)(brightness * LIGHT_PWM_FULL);
}

void Light::on(float brightness) {
  analogWrite(this->pin, (uint16_t)this->level(brightness));
}

void Light::off() { analogWrite(this->pin, LOW); }

void Light::blink(int times, float brightness, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    this->off();
    delay(offMs);
    this->on(brightness);
    delay(onMs);
  }
}

void Light::fadeOut(float from, int overMs) {
  const int start = this->level(from);
  // One write per level, plus the one that lands on dark. Dividing the time
  // by the writes rather than by the levels is what makes the last step land
  // inside overMs instead of one step past it.
  const int steps = start + 1;
  const int stepMs = overMs / steps;
  for (int value = start; value >= 0; value--) {
    analogWrite(this->pin, (uint16_t)value);
    delay(stepMs);
  }
}

Light::~Light() {
  // unused for now
}
