#include "Stepper.h"

#include "LimitSwitch.h"

Stepper* Stepper::instance_ = nullptr;

Stepper::Stepper(int dirPin, int stepPin)
    : dirPin_(dirPin),
      stepPin_(stepPin),
      position_(0),
      dir_(kDirCw),
      targetIntervalUs_(kStepIntervalMaxUs),
      enabled_(false),
      stepCountdownUs_(0),
      pulseActive_(false),
      timer_(nullptr) {}

void Stepper::begin() {
  pinMode(dirPin_, OUTPUT);
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  digitalWrite(dirPin_, dir_);

  instance_ = this;

  // Set up the step-generation timer (Arduino-ESP32 v2.x API): timer 0,
  // divider 80 -> 80 MHz / 80 = 1 MHz tick rate = 1 µs per count, count
  // up. Arm an auto-reload alarm every kTimerPeriodUs. The timer is
  // paused here; home() resumes it after the carriage is at a known
  // position.
  timer_ = timerBegin(0, 80, true);
  timerAttachInterrupt(timer_, &timerIsrTrampoline, true);
  timerAlarmWrite(timer_, kTimerPeriodUs, true);
  timerAlarmEnable(timer_);
  timerStop(timer_);
}

void Stepper::startTimer() {
  if (timer_) timerStart(timer_);
}

void Stepper::stopTimer() {
  if (timer_) timerStop(timer_);
}

long Stepper::positionMm() const {
  // Round to nearest mm so 159 steps (0.99 mm) reads as 1, not 0.
  long pos = position_;  // atomic 32-bit read
  return (pos + kStepsPerMm / 2) / kStepsPerMm;
}

void Stepper::stepPulseBlocking() {
  digitalWrite(stepPin_, HIGH);
  delayMicroseconds(kStepPulseWidthUs);
  digitalWrite(stepPin_, LOW);
}

void Stepper::homingStepBlocking(unsigned long intervalUs) {
  stepPulseBlocking();
  delayMicroseconds(intervalUs);
}

void Stepper::home(LimitSwitch& limit) {
  // Homing uses the blocking pulse path. Stop the timer first so the
  // ISR doesn't fight us for STEP_PIN or for position_.
  stopTimer();
  enabled_ = false;
  pulseActive_ = false;
  stepCountdownUs_ = 0;
  digitalWrite(stepPin_, LOW);

  // Pass 1: fast CCW until trip. If already engaged at boot we skip
  // straight to the back-off.
  digitalWrite(dirPin_, kDirCcw);
  dir_ = kDirCcw;
  delayMicroseconds(5);
  while (!limit.engaged()) {
    homingStepBlocking(kHomingIntervalFastUs);
  }

  // Back off CW until switch releases, then clearance steps.
  digitalWrite(dirPin_, kDirCw);
  dir_ = kDirCw;
  delayMicroseconds(5);
  while (limit.engaged()) {
    homingStepBlocking(kHomingIntervalFastUs);
  }
  for (int i = 0; i < kHomingBackoffSteps; i++) {
    homingStepBlocking(kHomingIntervalFastUs);
  }

  // Pass 2: slow CCW re-engage for a repeatable trip point.
  digitalWrite(dirPin_, kDirCcw);
  dir_ = kDirCcw;
  delayMicroseconds(5);
  while (!limit.engaged()) {
    homingStepBlocking(kHomingIntervalSlowUs);
  }

  // Final back off so we end with the switch released and clearance set.
  digitalWrite(dirPin_, kDirCw);
  dir_ = kDirCw;
  delayMicroseconds(5);
  while (limit.engaged()) {
    homingStepBlocking(kHomingIntervalSlowUs);
  }
  for (int i = 0; i < kHomingBackoffSteps; i++) {
    homingStepBlocking(kHomingIntervalSlowUs);
  }

  position_ = 0;
  startTimer();
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

  // Snapshot position once; it's written by the ISR.
  long pos = position_;
  bool zeroPosition = false;

  // Edge guards.
  if (desiredDir == kDirCw && pos >= kMaxPositionSteps) {
    wantMove = false;
  }
  if (desiredDir == kDirCcw) {
    if (limit.engaged()) {
      wantMove = false;
      if (pos < kMinPositionSteps + kDriftRecoveryWindowSteps) {
        zeroPosition = true;
      }
    } else if (pos <= kMinPositionSteps) {
      wantMove = false;
    }
  }

  if (wantMove) {
    if (desiredDir != dir_) {
      // Direction change: briefly disable stepping, wait for any in-
      // flight pulse to complete, switch DIR_PIN, then restart with a
      // fresh countdown.
      enabled_ = false;
      delayMicroseconds(kTimerPeriodUs + 5);
      digitalWrite(dirPin_, desiredDir);
      dir_ = desiredDir;
      delayMicroseconds(5);  // A4988 DIR-to-STEP setup
      portENTER_CRITICAL(&mux_);
      stepCountdownUs_ = targetInterval;
      targetIntervalUs_ = targetInterval;
      enabled_ = true;
      portEXIT_CRITICAL(&mux_);
    } else {
      targetIntervalUs_ = targetInterval;
      enabled_ = true;
    }
  } else {
    enabled_ = false;
  }

  // Drift recovery: zero position only after disabling stepping and
  // waiting out any in-flight pulse, so the ISR doesn't undo it.
  if (zeroPosition) {
    delayMicroseconds(kTimerPeriodUs + 5);
    portENTER_CRITICAL(&mux_);
    position_ = 0;
    portEXIT_CRITICAL(&mux_);
  }
}

void IRAM_ATTR Stepper::timerIsrTrampoline() {
  if (instance_) instance_->onTimer();
}

void IRAM_ATTR Stepper::onTimer() {
  if (pulseActive_) {
    // End of the STEP HIGH phase: drop the line, count the step, reload.
    digitalWrite(stepPin_, LOW);
    pulseActive_ = false;
    if (dir_ == kDirCw) position_++;
    else position_--;
    stepCountdownUs_ = targetIntervalUs_;
    return;
  }
  if (!enabled_) return;

  // Hard position bounds — the main loop normally enforces soft floor /
  // ceiling before we get here, but during display.display() the main
  // loop is paused for ~30 ms and these are the backstop so the motor
  // can't run past the physical end-stops in that gap.
  if (dir_ == kDirCw && position_ >= kMaxPositionSteps) return;
  if (dir_ == kDirCcw && position_ <= 0) return;

  if (stepCountdownUs_ <= kTimerPeriodUs) {
    digitalWrite(stepPin_, HIGH);
    pulseActive_ = true;
    stepCountdownUs_ = 0;
  } else {
    stepCountdownUs_ -= kTimerPeriodUs;
  }
}
