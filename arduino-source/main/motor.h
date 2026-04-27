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

  // Set smooth-mode speed by fraction. 0.0 -> SMOOTH_INTERVAL_US (slow),
  // 1.0 -> SMOOTH_MIN_INTERVAL_US (fast). Used by the joystick for
  // deflection-proportional speed control. Takes effect on the next pulse.
  void setSpeedFraction(float f);

  // Red STOP button. Halts whatever's running.
  void stopPressed();
}

// Defined in main.ino. Called from inside Motor's blocking step loop so
// serial input, buttons, and the display stay responsive during long spins.
void tick();
