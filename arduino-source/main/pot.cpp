#include "pot.h"

const uint8_t POT_PIN = A0;

// Tunable: how many motor steps the full ADC range traverses.
//   The user's pot rotates ~270° physically. With STEPS_PER_FULL_POT = 200
//   (one motor revolution = 360°), turning the knob through its full
//   physical range rotates the motor one complete revolution.
//   That's a 360° / 270° ≈ 1.33° motor per 1° knob ratio.
//
// To change the mapping:
//   - 1 motor rev per knob turn → STEPS_PER_FULL_POT = MOTOR_STEPS_PER_REV
//   - 2 motor revs per knob turn → STEPS_PER_FULL_POT = 2 × MOTOR_STEPS_PER_REV
//   - Less than 1 rev → smaller value
// MOTOR_STEPS_PER_REV is full-steps × A4988 microstep factor
// (NEMA 17 full step = 200; ×16 microstep = 3200).
const long MOTOR_STEPS_PER_REV  = 200;
const long STEPS_PER_FULL_POT   = 2000;  // 10 motor revolutions per full knob turn
const long ADC_RES              = 1024;

// Conversion: motor_steps = adc * STEPS_PER_FULL_POT / ADC_RES
// With the defaults above this is ~0.195 steps per ADC count, i.e. ~5
// counts per step — small enough that ±1–2 ADC noise stays below the
// step threshold and won't twitch the motor at rest.

const unsigned long ACTIVE_TIMEOUT_MS = 500;

namespace {
  long          lastReportedSteps = 0;
  unsigned long lastActiveMs      = 0;

  long adcToSteps(int adc) {
    return ((long)adc * STEPS_PER_FULL_POT) / ADC_RES;
  }

  long readSteps() {
    return adcToSteps(analogRead(POT_PIN));
  }
}

void Pot::begin() {
  lastReportedSteps = readSteps();
  lastActiveMs      = 0;
}

void Pot::resync() {
  lastReportedSteps = readSteps();
}

long Pot::pollDelta() {
  long current = readSteps();
  long delta   = current - lastReportedSteps;
  if (delta != 0) {
    lastReportedSteps = current;
    lastActiveMs      = millis();
  }
  return delta;
}

bool Pot::isActive() {
  if (lastActiveMs == 0) return false;
  return (millis() - lastActiveMs) < ACTIVE_TIMEOUT_MS;
}
