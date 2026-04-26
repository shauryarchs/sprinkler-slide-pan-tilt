#pragma once
#include <Arduino.h>
#include "motor.h"

namespace Display {
  bool begin();                                            // returns false on init failure
  void showBanner();                                       // intro screen, blocking ~1.5s
  void update(Mode mode, unsigned int smoothDelay, bool cw); // no-op unless state changed
}
