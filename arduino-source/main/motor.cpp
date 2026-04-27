#include "motor.h"

const int STEP_PIN = 3;
const int DIR_PIN  = 2;
const int DIR_CW   = LOW;
const int DIR_CCW  = HIGH;

const unsigned int SMOOTH_DEFAULT_DELAY_US = 50;
const unsigned int SMOOTH_START_DELAY_US   = 50;
const unsigned int SMOOTH_RAMP_STEP_US     = 10;
const unsigned int SMOOTH_MIN_DELAY_US     = 5;

namespace {
  Mode mode = MODE_IDLE;
  unsigned int smoothTargetDelay = SMOOTH_DEFAULT_DELAY_US;
  bool smoothCW = true;

  // Pulses between tick() calls in smooth mode. tick() runs serial polling,
  // button polling, and display refresh — all quick but variable. At a
  // 100 us half-period that variability shows up as audible/visible step
  // jitter. Batching to one tick per 16 pulses keeps cadence uniform while
  // staying well inside the 30 ms button debounce window.
  const int SMOOTH_TICK_EVERY = 16;

  void runSmooth() {
    bool dirOnPin = smoothCW;
    digitalWrite(DIR_PIN, dirOnPin ? DIR_CW : DIR_CCW);
    delayMicroseconds(5);
    unsigned int stepDelay = SMOOTH_START_DELAY_US;
    int sinceTick = SMOOTH_TICK_EVERY;   // tick on first iteration

    while (mode == MODE_SMOOTH) {
      if (++sinceTick > SMOOTH_TICK_EVERY) {
        sinceTick = 0;
        tick();
        if (mode != MODE_SMOOTH) return;

        // Direction reversed mid-spin: re-ramp from start so we don't lose
        // steps slamming the rotor into the opposite direction at full speed.
        if (smoothCW != dirOnPin) {
          dirOnPin = smoothCW;
          digitalWrite(DIR_PIN, dirOnPin ? DIR_CW : DIR_CCW);
          delayMicroseconds(5);
          stepDelay = SMOOTH_START_DELAY_US;
        }
      }

      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(stepDelay);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(stepDelay);

      if (stepDelay > smoothTargetDelay) {
        unsigned int dec = SMOOTH_RAMP_STEP_US;
        if (stepDelay - smoothTargetDelay < dec) stepDelay = smoothTargetDelay;
        else stepDelay -= dec;
      }
    }
  }
}

void Motor::begin() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
}

void Motor::setMode(Mode m) {
  mode = m;
  if (m == MODE_SMOOTH) smoothTargetDelay = SMOOTH_DEFAULT_DELAY_US;
}

Mode Motor::getMode() { return mode; }

unsigned int Motor::getSmoothDelay() { return smoothTargetDelay; }

bool Motor::isSmoothCW() { return smoothCW; }

void Motor::startSmooth(bool cw) {
  // Only reset target speed when entering smooth from another mode; a
  // direction flip while already smooth keeps the user's chosen speed.
  if (mode != MODE_SMOOTH) smoothTargetDelay = SMOOTH_DEFAULT_DELAY_US;
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
  unsigned int next = smoothTargetDelay / 2;
  if (next < SMOOTH_MIN_DELAY_US) next = SMOOTH_MIN_DELAY_US;
  smoothTargetDelay = next;
  Serial.print(F("faster, half-period now "));
  Serial.print(smoothTargetDelay);
  Serial.println(F(" us"));
}

void Motor::update() {
  if (mode == MODE_SMOOTH) runSmooth();
}
