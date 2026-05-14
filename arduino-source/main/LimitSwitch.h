#pragma once

#include <Arduino.h>

// Normally-open switch wired to a digital pin with the internal pull-up.
// Engaged == pin reads LOW.
class LimitSwitch {
 public:
  explicit LimitSwitch(int pin);
  void begin();
  bool engaged() const;

 private:
  int pin_;
};
