#include "TiltMotor3.h"

TiltMotor3* TiltMotor3::instance_ = nullptr;

TiltMotor3::TiltMotor3(int dirPin, int stepPin)
    : dirPin_(dirPin),
      stepPin_(stepPin),
      position_(0),
      dir_(kDirCw),
      targetIntervalUs_(kStepIntervalMaxUs),
      enabled_(false),
      stepCountdownUs_(0),
      pulseActive_(false),
      timer_(nullptr) {}

void TiltMotor3::begin() {
  pinMode(dirPin_, OUTPUT);
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  digitalWrite(dirPin_, dir_);

  instance_ = this;

  // Timer 2 — SliderMotor1 owns timer 0, PanMotor2 owns timer 1.
  timer_ = timerBegin(2, 80, true);
  timerAttachInterrupt(timer_, &timerIsrTrampoline, true);
  timerAlarmWrite(timer_, kTimerPeriodUs, true);
  timerAlarmEnable(timer_);
}

void TiltMotor3::stop() {
  enabled_ = false;
}

long TiltMotor3::positionDegrees() const {
  long pos = position_;
  return (pos * kMaxAngleDeg + kMaxPositionSteps / 2) / kMaxPositionSteps;
}

void TiltMotor3::update(int dial) {
  bool wantMove = (dial != 0);
  uint8_t desiredDir = (dial > 0) ? kDirCw : kDirCcw;
  int mag = (dial > 0) ? dial : -dial;
  unsigned long targetInterval = kStepIntervalMaxUs;
  if (mag > 0) {
    targetInterval = map(mag, 1, kEncoderRange,
                         kStepIntervalMaxUs, kStepIntervalMinUs);
  }

  long pos = position_;

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

void IRAM_ATTR TiltMotor3::timerIsrTrampoline() {
  if (instance_) instance_->onTimer();
}

void IRAM_ATTR TiltMotor3::onTimer() {
  if (pulseActive_) {
    digitalWrite(stepPin_, LOW);
    pulseActive_ = false;
    if (dir_ == kDirCw) position_++;
    else position_--;
    stepCountdownUs_ = targetIntervalUs_;
    return;
  }
  if (!enabled_) return;

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
