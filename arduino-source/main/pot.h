#pragma once
#include <Arduino.h>

namespace Pot {
  void begin();
  void resync();        // re-zero internal reference to current pot reading
  long pollDelta();     // signed motor-step delta since last call (CW = positive)
  bool isActive();      // true if pollDelta() returned non-zero recently
}
