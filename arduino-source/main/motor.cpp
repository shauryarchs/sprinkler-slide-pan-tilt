#include "motor.h"

const int STEP_PIN = 3;
const int DIR_PIN  = 2;
const int DIR_CW   = LOW;
const int DIR_CCW  = HIGH;

// Total step period in microseconds (half spent HIGH on STEP, half LOW).
// SMOOTH_INTERVAL_US is the SLOW end of the speed range (light joystick
// deflection); SMOOTH_MIN_INTERVAL_US is the FAST end (full deflection).
// Buttons / 'r' command also start at SMOOTH_INTERVAL_US.
// At 1/32 microstepping (6400 µsteps per motor revolution) on a NEMA 17:
//   1600 µs -> 625 steps/sec  -> ~5.9 RPM
//    800 µs -> 1250 steps/sec -> ~11.7 RPM
//    400 µs -> 2500 steps/sec -> ~23.4 RPM
//    200 µs -> 5000 steps/sec -> ~46.9 RPM
const unsigned int SMOOTH_INTERVAL_US     = 150;
const unsigned int SMOOTH_MIN_INTERVAL_US = 150;

namespace {
  Mode mode = MODE_IDLE;
  unsigned int smoothInterval = SMOOTH_INTERVAL_US;
  bool smoothCW = true;

  // Pulses between tick() calls. Tick polls serial, buttons, and the OLED
  // — variable-time work that adds jitter to every step if done too often.
  // Batching keeps step cadence even while staying inside the 30 ms button
  // debounce window.
  const int SMOOTH_TICK_EVERY = 16;

  void runSmooth() {
    digitalWrite(DIR_PIN, smoothCW ? DIR_CW : DIR_CCW);
    delayMicroseconds(5);
    bool dirOnPin = smoothCW;
    int sinceTick = SMOOTH_TICK_EVERY;   // tick on first iteration

    while (mode == MODE_SMOOTH) {
      if (++sinceTick > SMOOTH_TICK_EVERY) {
        sinceTick = 0;
        tick();
        if (mode != MODE_SMOOTH) return;

        // Direction reversed mid-spin: re-set DIR pin and keep going.
        if (smoothCW != dirOnPin) {
          dirOnPin = smoothCW;
          digitalWrite(DIR_PIN, dirOnPin ? DIR_CW : DIR_CCW);
          delayMicroseconds(5);
        }
      }

      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(smoothInterval / 2);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(smoothInterval / 2);
    }
  }
}

void Motor::begin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
}

void Motor::update() {
  if (mode == MODE_SMOOTH) runSmooth();
}

Mode Motor::getMode() { return mode; }

unsigned int Motor::getSmoothDelay() { return smoothInterval; }

bool Motor::isSmoothCW() { return smoothCW; }

void Motor::startSmooth(bool cw) {
  // Reset to default speed when entering from idle; a direction flip while
  // already running keeps whatever speed the user set with 'f'.
  if (mode != MODE_SMOOTH) smoothInterval = SMOOTH_INTERVAL_US;
  smoothCW = cw;
  mode = MODE_SMOOTH;
}

void Motor::stopPressed() {
  mode = MODE_IDLE;
}

void Motor::faster() {
  if (mode != MODE_SMOOTH) {
    Serial.println(F("(press 'r' first - 'f' only works in smooth mode)"));
    return;
  }
  unsigned int next = smoothInterval / 2;
  if (next < SMOOTH_MIN_INTERVAL_US) next = SMOOTH_MIN_INTERVAL_US;
  smoothInterval = next;
  Serial.print(F("faster, interval now "));
  Serial.print(smoothInterval);
  Serial.println(F(" us"));
}

void Motor::setSpeedFraction(float f) {
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  // f=0 -> SMOOTH_INTERVAL_US (slowest), f=1 -> SMOOTH_MIN_INTERVAL_US (fastest).
  unsigned int range = SMOOTH_INTERVAL_US - SMOOTH_MIN_INTERVAL_US;
  smoothInterval = SMOOTH_INTERVAL_US - (unsigned int)(f * range);
}
