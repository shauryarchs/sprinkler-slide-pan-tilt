#pragma once
#include <Arduino.h>

namespace Pot {
  void begin();

  // Cached internally (~5 ms TTL) so callers in the same tick don't
  // each pay the ~100 us cost of analogRead.
  bool isActive();              // |position - center| > deadband
  bool isCW();                  // true if pot deflected toward CW side
  unsigned int speedDelayUs();  // step half-period, smaller = faster
}
