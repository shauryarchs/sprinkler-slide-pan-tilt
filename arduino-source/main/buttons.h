#pragma once
#include <Arduino.h>

namespace Buttons {
  enum Event { EVENT_NONE, EVENT_LEFT, EVENT_RIGHT, EVENT_STOP };

  void begin();
  Event pollEvent();   // call from tick(); returns one press-edge event per call
}
