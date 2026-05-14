#pragma once

#include <Arduino.h>

// Third TMC2209-driven stepper. Same behavior as PanMotor2 — a bounded
// [0°, 180°] rotary motor with no limit switch and no homing — just
// running off its own hardware timer (timer 2) and on its own DIR/STEP
// pins (D10/D11 by default; the .ino wires them up). Position 0 is
// wherever the motor happens to be at boot, so it must be mechanically
// aligned before power-on.
//
// Microstepping assumption matches SliderMotor1 / PanMotor2: 1/32 → 6400
// microsteps per revolution → 3200 microsteps for 180°. Edit
// kStepsPerRev if your MS-pin wiring gives a different value.
class TiltMotor3 {
 public:
  static constexpr long kStepsPerRev = 6400;
  static constexpr long kMaxAngleDeg = 180;
  static constexpr long kMaxPositionSteps =
      kStepsPerRev * kMaxAngleDeg / 360;  // 3200

  static constexpr unsigned int kStepPulseWidthUs = 3;
  static constexpr unsigned long kStepIntervalMinUs = 120;
  static constexpr unsigned long kStepIntervalMaxUs = 3000;

  static constexpr int kDirCw = LOW;
  static constexpr int kDirCcw = HIGH;
  static constexpr int kEncoderRange = 20;

  static constexpr unsigned long kTimerPeriodUs = 20;

  TiltMotor3(int dirPin, int stepPin);

  void begin();
  void update(int dial);
  void stop();

  long positionSteps() const { return position_; }
  long positionDegrees() const;

 private:
  static TiltMotor3* instance_;
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

static_assert(TiltMotor3::kStepIntervalMinUs < TiltMotor3::kStepIntervalMaxUs,
              "min step interval must be smaller than max (fast < slow)");
static_assert(TiltMotor3::kMaxPositionSteps > 0,
              "max position must be positive");
static_assert(TiltMotor3::kStepIntervalMinUs % TiltMotor3::kTimerPeriodUs == 0,
              "step intervals should be a multiple of the timer period");
