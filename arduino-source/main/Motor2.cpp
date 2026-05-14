#include "Motor2.h"

Motor2* Motor2::instance_ = nullptr;

Motor2::Motor2(int dirPin, int stepPin)
    : dirPin_(dirPin),
      stepPin_(stepPin),
      position_(0),
      dir_(kDirCw),
      targetIntervalUs_(kStepIntervalMaxUs),
      enabled_(false),
      stepCountdownUs_(0),
      pulseActive_(false),
      timer_(nullptr) {}

void Motor2::begin() {
  pinMode(dirPin_, OUTPUT);
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  digitalWrite(dirPin_, dir_);

  instance_ = this;

  // Timer 1 — Stepper uses timer 0. 80 MHz / 80 = 1 MHz tick.
  timer_ = timerBegin(1, 80, true);
  timerAttachInterrupt(timer_, &timerIsrTrampoline, true);
  timerAlarmWrite(timer_, kTimerPeriodUs, true);
  timerAlarmEnable(timer_);
  // No homing for motor2, so start the timer running immediately.
  // update() will arm enabled_ when the user dials.
}

void Motor2::stop() {
  enabled_ = false;
}

long Motor2::positionDegrees() const {
  long pos = position_;
  return (pos * kMaxAngleDeg + kMaxPositionSteps / 2) / kMaxPositionSteps;
}

void Motor2::update(int dial) {
  bool wantMove = (dial != 0);
  uint8_t desiredDir = (dial > 0) ? kDirCw : kDirCcw;
  int mag = (dial > 0) ? dial : -dial;
  unsigned long targetInterval = kStepIntervalMaxUs;
  if (mag > 0) {
    targetInterval = map(mag, 1, kEncoderRange,
                         kStepIntervalMaxUs, kStepIntervalMinUs);
  }

  long pos = position_;

  // Bounds: motor2 lives in [0, kMaxPositionSteps]. No drift recovery —
  // there's no limit switch to act as a reference.
  if (desiredDir == kDirCw && pos >= kMaxPositionSteps) wantMove = false;
  if (desiredDir == kDirCcw && pos <= 0) wantMove = false;

  if (wantMove) {
    if (desiredDir != dir_) {
      enabled_ = false;
      delayMicroseconds(kTimerPeriodUs + 5);
      digitalWrite(dirPin_, desiredDir);
      dir_ = desiredDir;
      delayMicroseconds(5);
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
}

void IRAM_ATTR Motor2::timerIsrTrampoline() {
  if (instance_) instance_->onTimer();
}

void IRAM_ATTR Motor2::onTimer() {
  if (pulseActive_) {
    digitalWrite(stepPin_, LOW);
    pulseActive_ = false;
    if (dir_ == kDirCw) position_++;
    else position_--;
    stepCountdownUs_ = targetIntervalUs_;
    return;
  }
  if (!enabled_) return;

  // Hard bounds backstop — main loop normally catches this earlier but
  // the ISR keeps running during display.display() blocks, so without
  // this check the motor could overrun while the loop is paused.
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
