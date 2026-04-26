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
  void startSmooth(bool cw);  // enter smooth mode in the given direction
  bool isSmoothCW();

  // Red STOP button. Halts whatever's running and latches the pot off so
  // it cannot restart the motor on its own. Pressing STOP again while
  // already idle toggles the pot back on.
  void stopPressed();
  bool isPotDisabled();
}

// Defined in main.ino. Called between motor pulses so the rest of the
// system (serial input, display) stays responsive during long moves.
void tick();
