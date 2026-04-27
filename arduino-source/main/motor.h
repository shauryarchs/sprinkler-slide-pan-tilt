#pragma once
#include <Arduino.h>

enum Mode { MODE_IDLE, MODE_SMOOTH };

namespace Motor {
  void begin();
  void update();              // call from loop() as often as possible
  Mode getMode();
  unsigned int getSmoothDelay();
  void faster();              // double smooth-mode max speed
  void startSmooth(bool cw);
  bool isSmoothCW();

  // Red STOP button. Halts whatever's running.
  void stopPressed();
}

// Defined in main.ino. Called from inside Motor's blocking step loop so
// serial input, buttons, and the display stay responsive during long spins.
void tick();
