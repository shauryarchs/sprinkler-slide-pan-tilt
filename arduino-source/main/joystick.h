#pragma once
#include <Arduino.h>

namespace Joystick {
  enum Event { EVENT_NONE, EVENT_UP, EVENT_DOWN, EVENT_CENTER };

  void begin();
  Event poll();   // call from tick(); returns one transition event per call

  // 0.0 when in the center deadband, 1.0 at full deflection. Sign is
  // dropped — direction is reported via poll() events. Reads from the same
  // sample taken by the most recent poll() call, so call poll() first.
  float deflection();
}
