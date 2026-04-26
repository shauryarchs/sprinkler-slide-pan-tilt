#pragma once
#include <Arduino.h>

enum Mode { MODE_IDLE, MODE_DANCE, MODE_SMOOTH };

namespace Motor {
  void begin();
  void update();              // call from loop(); runs current mode
  void setMode(Mode m);
  Mode getMode();
  unsigned int getSmoothDelay();
  void faster();              // halve smooth-mode delay (doubles speed)
}

// Defined in main.ino. Called between motor pulses so the rest of the
// system (serial input, display) stays responsive during long moves.
void tick();
