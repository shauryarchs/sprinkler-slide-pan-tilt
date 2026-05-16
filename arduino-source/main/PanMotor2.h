#pragma once

#include <Arduino.h>

// Second TMC2209-driven stepper. Same step/dir interface as SliderMotor1
// but no limit switch and no homing — the motor is assumed to be at "0°"
// at boot, so it must be mechanically aligned before power-up. Travel
// is bounded in software to [0°, 360°].
//
// Uses its own hardware timer (timer 1) for step generation, mirroring
// the SliderMotor1 class so a blocking display.display() in the main
// loop doesn't pause the pulse stream.
//
// Assumes the TMC2209 MS pins are configured for 1/32 microstepping,
// giving 200 * 32 = 6400 microsteps per motor revolution. With a 1:1
// shaft coupling that's 6400/360 microsteps/degree, so 360° is 6400
// microsteps. Adjust kStepsPerRev if your microstepping or gearing
// differs.
class PanMotor2 {
 public:
  static constexpr long kStepsPerRev = 6400;
  static constexpr long kMaxAngleDeg = 360;
  static constexpr long kMaxPositionSteps =
      kStepsPerRev * kMaxAngleDeg / 360;  // 6400

  static constexpr unsigned int kStepPulseWidthUs = 3;
  static constexpr unsigned long kStepIntervalMinUs = 120;   // fastest
  static constexpr unsigned long kStepIntervalMaxUs = 3000;  // slowest

  static constexpr int kDirCw = LOW;
  static constexpr int kDirCcw = HIGH;
  static constexpr int kEncoderRange = 20;  // matches Encoder::kRange

  static constexpr unsigned long kTimerPeriodUs = 20;

  PanMotor2(int dirPin, int stepPin);

  void begin();
  // Per-loop call: writes target step interval / direction / enabled
  // based on the signed dial value.
  void update(int dial);
  void stop();

  // Subtract kMaxPositionSteps from the step counter without stopping
  // the timer. Use from a continuous-rotation mode to keep position_
  // from crossing the ISR's soft-ceiling check.
  void wrapPosition();

  long positionSteps() const { return position_; }
  long positionDegrees() const;

 private:
  static PanMotor2* instance_;
  static void IRAM_ATTR timerIsrTrampoline();
  void IRAM_ATTR onTimer();

  int dirPin_;
  int stepPin_;

  volatile long position_;
  volatile uint8_t dir_;
  volatile unsigned long targetIntervalUs_;
  volatile bool enabled_;
  volatile unsigned long stepCountdownUs_;
  volatile bool pulseActive_;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  hw_timer_t* timer_;
};

static_assert(PanMotor2::kStepIntervalMinUs < PanMotor2::kStepIntervalMaxUs,
              "min step interval must be smaller than max (fast < slow)");
static_assert(PanMotor2::kMaxPositionSteps > 0,
              "max position must be positive");
static_assert(PanMotor2::kStepIntervalMinUs % PanMotor2::kTimerPeriodUs == 0,
              "step intervals should be a multiple of the timer period");
