#include "PanMotor2.h"

PanMotor2* PanMotor2::instance_ = nullptr;

PanMotor2::PanMotor2(int dirPin, int stepPin)
    : dirPin_(dirPin),
      stepPin_(stepPin),
      position_(0),
      dir_(kDirCw),
      targetIntervalUs_(kStepIntervalMaxUs),
      enabled_(false),
      stepCountdownUs_(0),
      pulseActive_(false),
      timer_(nullptr) {}

void PanMotor2::begin() {
  pinMode(dirPin_, OUTPUT);
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  digitalWrite(dirPin_, dir_);

  instance_ = this;

  // Timer 1 — SliderMotor1 uses timer 0. 80 MHz / 80 = 1 MHz tick.
  timer_ = timerBegin(1, 80, true);
  timerAttachInterrupt(timer_, &timerIsrTrampoline, true);
  timerAlarmWrite(timer_, kTimerPeriodUs, true);
  timerAlarmEnable(timer_);
  // No homing for motor2, so start the timer running immediately.
  // update() will arm enabled_ when the user dials.
}

void PanMotor2::stop() {
  enabled_ = false;
}

void PanMotor2::stepBy(int delta) {
  if (delta == 0) return;

  // Ensure ISR-driven stepping is off and any in-flight pulse has
  // finished, so we own STEP/DIR while we drive them by hand.
  enabled_ = false;
  delayMicroseconds(kTimerPeriodUs + 5);

  uint8_t newDir = (delta > 0) ? kDirCw : kDirCcw;
  if (newDir != dir_) {
    digitalWrite(dirPin_, newDir);
    dir_ = newDir;
    delayMicroseconds(5);  // DIR-to-STEP setup
  }

  int count = (delta > 0) ? delta : -delta;
  for (int i = 0; i < count; i++) {
    digitalWrite(stepPin_, HIGH);
    delayMicroseconds(kStepPulseWidthUs);
    digitalWrite(stepPin_, LOW);
    delayMicroseconds(kSetupStepIntervalUs);
    portENTER_CRITICAL(&mux_);
    if (newDir == kDirCw) position_++;
    else position_--;
    portEXIT_CRITICAL(&mux_);
  }
}

void PanMotor2::zeroPosition() {
  enabled_ = false;
  delayMicroseconds(kTimerPeriodUs + 5);
  portENTER_CRITICAL(&mux_);
  position_ = 0;
  portEXIT_CRITICAL(&mux_);
}

long PanMotor2::positionDegrees() const {
  long pos = position_;
  return (pos * kMaxAngleDeg + kMaxPositionSteps / 2) / kMaxPositionSteps;
}

void PanMotor2::update(int dial) {
  bool wantMove = (dial != 0);
  uint8_t desiredDir = (dial > 0) ? kDirCw : kDirCcw;
  int mag = (dial > 0) ? dial : -dial;
  unsigned long targetInterval = kStepIntervalMaxUs;
  if (mag > 0) {
    targetInterval = map(mag, 1, kEncoderRange,
                         kStepIntervalMaxUs, kStepIntervalMinUs);
  }

  long pos = position_;

  // Bounds: motor2 lives in [kMinPositionSteps, kMaxPositionSteps], i.e.
  // ±105° around the boot reference. No drift recovery — there's no
  // limit switch to act as a reference.
  if (desiredDir == kDirCw && pos >= kMaxPositionSteps) wantMove = false;
  if (desiredDir == kDirCcw && pos <= kMinPositionSteps) wantMove = false;

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

void IRAM_ATTR PanMotor2::timerIsrTrampoline() {
  if (instance_) instance_->onTimer();
}

void IRAM_ATTR PanMotor2::onTimer() {
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
  if (dir_ == kDirCcw && position_ <= kMinPositionSteps) return;

  if (stepCountdownUs_ <= kTimerPeriodUs) {
    digitalWrite(stepPin_, HIGH);
    pulseActive_ = true;
    stepCountdownUs_ = 0;
  } else {
    stepCountdownUs_ -= kTimerPeriodUs;
  }
}
