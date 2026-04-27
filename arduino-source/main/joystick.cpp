#include "joystick.h"

const uint8_t JOY_Y_PIN = A0;

// ADC counts past rest before any motion registers. Wide enough to ignore
// drift and ADC noise on a cheap module without per-boot calibration.
const int JOY_DEAD_HALF = 100;

// Max useful deflection from rest. Cheap modules often don't reach 0/1023
// at the extremes; clamping keeps the speed mapping linear and predictable.
const int JOY_MAX_DEFLECT = 412;

namespace {
  enum State { S_CENTER, S_UP, S_DOWN };
  State lastState = S_CENTER;
  int restValue   = 512;   // captured in begin() so a slightly-off-center
                           // stick doesn't fire phantom events at boot
  int lastReading = 512;   // most recent analogRead, shared with deflection()

  State sample() {
    lastReading = analogRead(JOY_Y_PIN);
    int delta = lastReading - restValue;
    if (delta >  JOY_DEAD_HALF) return S_UP;
    if (delta < -JOY_DEAD_HALF) return S_DOWN;
    return S_CENTER;
  }
}

void Joystick::begin() {
  restValue   = analogRead(JOY_Y_PIN);
  lastReading = restValue;
  lastState   = S_CENTER;
}

Joystick::Event Joystick::poll() {
  State now = sample();
  if (now == lastState) return EVENT_NONE;
  lastState = now;
  switch (now) {
    case S_UP:     return EVENT_UP;
    case S_DOWN:   return EVENT_DOWN;
    case S_CENTER: return EVENT_CENTER;
  }
  return EVENT_NONE;
}

float Joystick::deflection() {
  int delta = lastReading - restValue;
  if (delta < 0) delta = -delta;
  delta -= JOY_DEAD_HALF;
  if (delta < 0) return 0.0;
  if (delta > JOY_MAX_DEFLECT) delta = JOY_MAX_DEFLECT;
  return (float)delta / (float)JOY_MAX_DEFLECT;
}
