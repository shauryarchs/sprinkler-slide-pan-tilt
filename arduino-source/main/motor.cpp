#include "motor.h"
#include "pot.h"

const int STEP_PIN = 3;
const int DIR_PIN  = 2;
const int DIR_CW   = LOW;
const int DIR_CCW  = HIGH;

const unsigned int SMOOTH_DEFAULT_DELAY_US = 233;  // 3x the previous 700 us speed
const unsigned int SMOOTH_START_DELAY_US   = 2000;
const unsigned int SMOOTH_RAMP_STEP_US     = 4;
const unsigned int SMOOTH_MIN_DELAY_US     = 150;

namespace {
  Mode mode = MODE_IDLE;
  unsigned int smoothTargetDelay = SMOOTH_DEFAULT_DELAY_US;
  bool smoothCW = true;

  struct Move {
    bool clockwise;
    unsigned int steps;
    unsigned int delayUs;
    unsigned int pauseMsAfter;
  };

  const Move dance[] = {
    { true,   40,  600,  60 },
    { false,  40,  600,  60 },
    { true,   40,  600,  60 },
    { false,  40,  600, 120 },
    { true,  100,  400, 200 },
    { false,  50, 1400, 250 },
    { true,   12,  700,  40 },
    { false,  12,  700,  40 },
    { true,   12,  700,  40 },
    { false,  12,  700,  40 },
    { true,   12,  700,  40 },
    { false,  12,  700, 200 },
    { true,    8,  500,  25 },
    { false,   8,  500,  25 },
    { true,    8,  500,  25 },
    { false,   8,  500,  25 },
    { true,    8,  500,  25 },
    { false,   8,  500, 250 },
    { true,  300,  450, 300 },
    { false, 300,  450, 600 },
  };
  const size_t DANCE_LEN = sizeof(dance) / sizeof(dance[0]);

  void runDanceMove(const Move& m) {
    digitalWrite(DIR_PIN, m.clockwise ? DIR_CW : DIR_CCW);
    delayMicroseconds(5);

    for (unsigned int i = 0; i < m.steps; i++) {
      tick();
      if (mode != MODE_DANCE) return;
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(m.delayUs);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(m.delayUs);
    }

    unsigned long start = millis();
    while (millis() - start < m.pauseMsAfter) {
      tick();
      if (mode != MODE_DANCE) return;
    }
  }

  void runDance() {
    for (size_t i = 0; i < DANCE_LEN && mode == MODE_DANCE; i++) {
      runDanceMove(dance[i]);
    }
  }

  // Step half-period when tracking the pot. Short enough that the motor
  // can keep up with reasonable knob-turning speeds without missing steps.
  const unsigned int POT_STEP_DELAY_US = 500;

  void runPot() {
    // Active only when mode == MODE_IDLE. The pot acts as a position
    // encoder: each call we ask Pot for the signed step delta accumulated
    // since last poll, then pulse the motor that many times in the right
    // direction. No deflection ⇒ no movement.
    long delta = Pot::pollDelta();
    if (delta == 0) return;

    bool cw = delta > 0;
    long n  = (delta > 0) ? delta : -delta;

    digitalWrite(DIR_PIN, cw ? DIR_CW : DIR_CCW);
    delayMicroseconds(5);

    for (long i = 0; i < n; i++) {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(POT_STEP_DELAY_US);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(POT_STEP_DELAY_US);

      // Stay responsive to button/serial input mid-burst, but not on
      // every step (the I2C/serial overhead would slow tracking).
      if ((i & 7) == 0) {
        tick();
        if (mode != MODE_IDLE) return;
      }
    }
  }

  void runSmooth() {
    bool dirOnPin = smoothCW;
    digitalWrite(DIR_PIN, dirOnPin ? DIR_CW : DIR_CCW);
    delayMicroseconds(5);
    unsigned int stepDelay = SMOOTH_START_DELAY_US;

    while (mode == MODE_SMOOTH) {
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
  // When entering IDLE from another mode, re-zero the pot's reference
  // so the motor doesn't immediately jump to "catch up" with any pot
  // movement that happened while we were in DANCE/SMOOTH.
  if (mode != MODE_IDLE && m == MODE_IDLE) Pot::resync();
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
  switch (mode) {
    case MODE_DANCE:  runDance();  break;
    case MODE_SMOOTH: runSmooth(); break;
    case MODE_IDLE:   runPot();    break;
  }
}
