#pragma once

#include <Arduino.h>

class LimitSwitch;

// A4988-style step/dir stepper driver with two-pass homing, soft floor /
// ceiling, and a dial-driven speed setpoint.
//
// Step generation runs from a hardware timer ISR rather than the main
// loop, so the ~30 ms blocking display.display() refresh no longer pauses
// the pulse stream (which previously caused an audible jerk and missed
// steps at high speeds). The main loop's update() only writes the target
// interval / direction / enabled flag; the ISR produces the actual pulses.
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

  // STEP pulse must be >=1 µs on the A4988. With the timer-ISR scheme the
  // pulse is held HIGH for one timer period (kTimerPeriodUs), which is
  // well above the 1 µs minimum — the homing path still uses the
  // dedicated 3 µs blocking pulse since it doesn't go through the timer.
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

  // Hardware-timer tick. Step intervals are quantized to multiples of
  // this: 20 µs gives 6 ticks of resolution at the min step interval
  // (120 µs) and 150 ticks at the max (3000 µs).
  static constexpr unsigned long kTimerPeriodUs = 20;

  Stepper(int dirPin, int stepPin);

  void begin();
  void home(LimitSwitch& limit);
  void update(int dial, LimitSwitch& limit);

  long positionSteps() const { return position_; }
  long positionMm() const;
  unsigned long currentSpeedTenthsMmPerSec() const;

 private:
  static Stepper* instance_;
  static void IRAM_ATTR timerIsrTrampoline();
  void IRAM_ATTR onTimer();

  void startTimer();
  void stopTimer();
  void stepPulseBlocking();
  void homingStepBlocking(unsigned long intervalUs);

  int dirPin_;
  int stepPin_;

  // Shared between main loop and timer ISR. The ISR runs on whichever
  // core called timerBegin(), which is the Arduino loop core, so volatile
  // + single-word atomicity is sufficient — but we still gate multi-field
  // updates and position writes with portMUX to be safe under FreeRTOS
  // and cross-core access.
  volatile long position_;
  volatile uint8_t dir_;
  volatile unsigned long targetIntervalUs_;
  volatile bool enabled_;
  volatile unsigned long stepCountdownUs_;
  volatile bool pulseActive_;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  // Diagnostic snapshot for currentSpeedTenthsMmPerSec().
  unsigned long lastTargetInterval_;
  bool lastWantMove_;

  hw_timer_t* timer_;
};

static_assert(Stepper::kStepIntervalMinUs < Stepper::kStepIntervalMaxUs,
              "min step interval must be smaller than max (fast < slow)");
static_assert(Stepper::kMinPositionSteps < Stepper::kMaxPositionSteps,
              "soft floor must be below the far limit");
static_assert(Stepper::kHomingBackoffSteps > 0,
              "back-off must clear the switch");
static_assert(Stepper::kStepIntervalMinUs % Stepper::kTimerPeriodUs == 0,
              "step intervals should be a multiple of the timer period");
