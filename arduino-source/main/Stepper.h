#pragma once

#include <Arduino.h>

class LimitSwitch;

// A4988-style step/dir stepper driver with two-pass homing, soft floor /
// ceiling, and a dial-driven speed setpoint.
//
// Microstepping (MS1=VDD, MS2=GND on the A4988) and the GT2/20T belt
// give 6400 steps/rev * 1/(40 mm/rev) = 160 steps/mm.
class Stepper {
 public:
  static constexpr long kStepsPerMm = 160;
  static constexpr long kMaxPositionMm = 1000;
  static constexpr long kMinPositionMm = 5;
  static constexpr long kMaxPositionSteps = kMaxPositionMm * kStepsPerMm;
  static constexpr long kMinPositionSteps = kMinPositionMm * kStepsPerMm;

  // STEP pulse must be >=1 µs on the A4988; 3 µs leaves margin without
  // capping the max step rate.
  static constexpr unsigned int kStepPulseWidthUs = 3;

  // Dial magnitude maps linearly into [kStepIntervalMinUs, kStepIntervalMaxUs].
  static constexpr unsigned long kStepIntervalMinUs = 120;   // ~52 mm/s
  static constexpr unsigned long kStepIntervalMaxUs = 3000;  // ~2 mm/s

  // Two-pass homing: fast approach, back off, slow re-engage, final back off.
  static constexpr unsigned long kHomingIntervalFastUs = 200;  // ~30 mm/s
  static constexpr unsigned long kHomingIntervalSlowUs = 800;  // ~7.5 mm/s
  static constexpr int kHomingBackoffSteps = 160;              // ~1 mm

  // CCW limit trips within this many steps of the soft floor auto-zero
  // the counter. Larger trips require an explicit re-home.
  static constexpr long kDriftRecoveryWindowSteps = 1600;  // ~10 mm

  static constexpr int kDirCw = LOW;
  static constexpr int kDirCcw = HIGH;

  // Dial range matches Encoder::kRange.
  static constexpr int kEncoderRange = 20;

  Stepper(int dirPin, int stepPin);

  void begin();
  void home(LimitSwitch& limit);
  void update(int dial, LimitSwitch& limit);

  long positionSteps() const { return position_; }
  long positionMm() const;
  unsigned long currentSpeedTenthsMmPerSec() const;

 private:
  void stepPulse();
  void homingStep(unsigned long intervalUs);

  int dirPin_;
  int stepPin_;
  long position_;
  uint8_t dir_;
  unsigned long lastStepUs_;
  unsigned long lastTargetInterval_;
  bool lastWantMove_;
};

static_assert(Stepper::kStepIntervalMinUs < Stepper::kStepIntervalMaxUs,
              "min step interval must be smaller than max (fast < slow)");
static_assert(Stepper::kMinPositionSteps < Stepper::kMaxPositionSteps,
              "soft floor must be below the far limit");
static_assert(Stepper::kHomingBackoffSteps > 0,
              "back-off must clear the switch");
