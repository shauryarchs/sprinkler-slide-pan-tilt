#include "pot.h"

const uint8_t POT_PIN = A0;

// Hardware-dependent — tune to your pot and stepper if 1° pot ≠ 1° motor.
//   POT_USABLE_DEG    : mechanical degrees the pot's wiper traverses
//                       (typical linear pots ~270°; some are 300°)
//   MOTOR_STEPS_PER_REV : full-steps per revolution × A4988 microstep factor
//                         (NEMA 17 full step = 200; ×16 microstep = 3200)
const long POT_USABLE_DEG      = 270;
const long MOTOR_STEPS_PER_REV = 200;
const long ADC_RES             = 1024;

// Conversion: motor_steps = adc * MOTOR_STEPS_PER_REV * POT_USABLE_DEG
//                                / (ADC_RES * 360)
// With the defaults above this is ~0.146 steps per ADC count, i.e. ~7
// counts per step — small enough that typical ±1–2 ADC noise stays below
// the step threshold and won't twitch the motor at rest.

const unsigned long ACTIVE_TIMEOUT_MS = 500;

namespace {
  long          lastReportedSteps = 0;
  unsigned long lastActiveMs      = 0;

  long adcToSteps(int adc) {
    return ((long)adc * MOTOR_STEPS_PER_REV * POT_USABLE_DEG) / (ADC_RES * 360);
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
