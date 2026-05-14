#include "Stepper.h"

#include "LimitSwitch.h"

Stepper::Stepper(int dirPin, int stepPin)
    : dirPin_(dirPin),
      stepPin_(stepPin),
      position_(0),
      dir_(kDirCw),
      lastStepUs_(0),
      lastTargetInterval_(kStepIntervalMaxUs),
      lastWantMove_(false) {}

void Stepper::begin() {
  pinMode(dirPin_, OUTPUT);
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  digitalWrite(dirPin_, dir_);
}

long Stepper::positionMm() const {
  // Round to nearest mm so 159 steps (0.99 mm) reads as 1, not 0.
  return (position_ + kStepsPerMm / 2) / kStepsPerMm;
}

unsigned long Stepper::currentSpeedTenthsMmPerSec() const {
  if (!lastWantMove_) return 0;
  return (10UL * 1000000UL) /
         (lastTargetInterval_ * (unsigned long)kStepsPerMm);
}

void Stepper::stepPulse() {
  digitalWrite(stepPin_, HIGH);
  delayMicroseconds(kStepPulseWidthUs);
  digitalWrite(stepPin_, LOW);
}

void Stepper::homingStep(unsigned long intervalUs) {
  stepPulse();
  delayMicroseconds(intervalUs);
}

void Stepper::home(LimitSwitch& limit) {
  // Pass 1: fast CCW until trip. If already engaged at boot we skip
  // straight to the back-off.
  digitalWrite(dirPin_, kDirCcw);
  dir_ = kDirCcw;
  delayMicroseconds(5);
  while (!limit.engaged()) {
    homingStep(kHomingIntervalFastUs);
  }

  // Back off CW until switch releases, then clearance steps.
  digitalWrite(dirPin_, kDirCw);
  dir_ = kDirCw;
  delayMicroseconds(5);
  while (limit.engaged()) {
    homingStep(kHomingIntervalFastUs);
  }
  for (int i = 0; i < kHomingBackoffSteps; i++) {
    homingStep(kHomingIntervalFastUs);
  }

  // Pass 2: slow CCW re-engage for a repeatable trip point.
  digitalWrite(dirPin_, kDirCcw);
  dir_ = kDirCcw;
  delayMicroseconds(5);
  while (!limit.engaged()) {
    homingStep(kHomingIntervalSlowUs);
  }

  // Final back off so we end with the switch released and clearance set.
  digitalWrite(dirPin_, kDirCw);
  dir_ = kDirCw;
  delayMicroseconds(5);
  while (limit.engaged()) {
    homingStep(kHomingIntervalSlowUs);
  }
  for (int i = 0; i < kHomingBackoffSteps; i++) {
    homingStep(kHomingIntervalSlowUs);
  }

  position_ = 0;
  lastStepUs_ = micros();
}

void Stepper::update(int dial, LimitSwitch& limit) {
  bool wantMove = (dial != 0);
  uint8_t desiredDir = (dial > 0) ? kDirCw : kDirCcw;
  int mag = (dial > 0) ? dial : -dial;
  unsigned long targetInterval = kStepIntervalMaxUs;
  if (mag > 0) {
    targetInterval = map(mag, 1, kEncoderRange,
                         kStepIntervalMaxUs, kStepIntervalMinUs);
  }

  // Edge guards.
  if (desiredDir == kDirCw && position_ >= kMaxPositionSteps) {
    wantMove = false;
  }
  if (desiredDir == kDirCcw) {
    if (limit.engaged()) {
      wantMove = false;
      if (position_ < kMinPositionSteps + kDriftRecoveryWindowSteps) {
        position_ = 0;
      }
    } else if (position_ <= kMinPositionSteps) {
      wantMove = false;
    }
  }

  if (wantMove) {
    if (desiredDir != dir_) {
      dir_ = desiredDir;
      digitalWrite(dirPin_, desiredDir);
      delayMicroseconds(5);
      lastStepUs_ = micros();
    }
    unsigned long nowUs = micros();
    if (nowUs - lastStepUs_ >= targetInterval) {
      stepPulse();
      if (dir_ == kDirCw) position_++;
      else position_--;
      lastStepUs_ = nowUs;
    }
  }

  lastTargetInterval_ = targetInterval;
  lastWantMove_ = wantMove;
}
