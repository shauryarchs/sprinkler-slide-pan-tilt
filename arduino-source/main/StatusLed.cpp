#include "StatusLed.h"

StatusLed::StatusLed(int pin, unsigned long blinkIntervalMs)
    : pin_(pin),
      blinkIntervalMs_(blinkIntervalMs),
      blinking_(false),
      ledState_(false),
      lastBlinkMs_(0) {}

void StatusLed::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void StatusLed::setBlinking(bool blinking) {
  blinking_ = blinking;
  if (!blinking_) {
    ledState_ = false;
    digitalWrite(pin_, LOW);
  }
}

void StatusLed::update() {
  if (!blinking_) return;
  unsigned long now = millis();
  if (now - lastBlinkMs_ >= blinkIntervalMs_) {
    ledState_ = !ledState_;
    digitalWrite(pin_, ledState_ ? HIGH : LOW);
    lastBlinkMs_ = now;
  }
}
