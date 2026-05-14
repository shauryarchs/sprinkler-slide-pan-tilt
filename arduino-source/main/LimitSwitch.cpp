#include "LimitSwitch.h"

LimitSwitch::LimitSwitch(int pin) : pin_(pin) {}

void LimitSwitch::begin() {
  pinMode(pin_, INPUT_PULLUP);
}

bool LimitSwitch::engaged() const {
  return digitalRead(pin_) == LOW;
}
