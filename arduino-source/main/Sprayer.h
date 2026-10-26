#pragma once

#include <Arduino.h>

class AutoAim;

// ---------------------------------------------------------------------
// Water pump control: sprays when auto-aim reports it is on target.
//
// Hardware: D2 -> 1 kOhm -> NPN transistor (2N2222A) base; collector to
// the relay module's IN, emitter to GND. The relay module (low-trigger,
// SRD-05VDC-SL-C) takes VCC from VBUS and switches the pump's own 5 V
// supply through COM/NO. D2 HIGH = pump on, D2 LOW = pump off; the pump
// is also off while the board boots or is unplugged.
//
// Automatic spraying is ON at boot (kAutoAtBoot; owner sign-off after
// hardware validation, #53). "spray off" in the Serial Monitor disables it
// until reboot ("spray on" re-enables). When enabled, the pump starts as
// soon as auto-aim reports on-target, keeps running while auto-aim stays
// engaged with the target (including small re-aim corrections), and
// stops when:
//   - the hotspot is lost (fire out or out of view),
//   - auto-aim pauses or leaves the menu, or "aim off" / "spray off",
//   - any user input (encoder or app/website command),
//   - kMaxSprayMs of continuous spraying (then waits kCooldownMs before
//     it may spray again).
//
// Manual runs last at most kManualMaxS from the serial "pump on", or
// kRemoteMaxS from the app's pumpOn, and stop on "pump off", pumpOff, the
// encoder, or a remote Stop (enterMode menu). Other remote commands (mode,
// speed, nudge) leave a manual run going so the user can aim while spraying.
//
// Serial commands:
//   spray on | spray off | spray status
//   pump on [s] (manual test run, 1..kManualMaxS, default kManualDefaultS)
//   pump off
// ---------------------------------------------------------------------
class Sprayer {
 public:
  // true: spray on target from boot (owner sign-off 2026-10-24, #53).
  // Set to false to require "spray on" after every boot again.
  static constexpr bool kAutoAtBoot = true;
  static constexpr unsigned long kMaxSprayMs = 60000;
  static constexpr unsigned long kCooldownMs = 10000;
  static constexpr unsigned long kManualDefaultS = 3;
  static constexpr unsigned long kManualMaxS = 30;
  // Longest manual run a remote pumpOn can request.
  static constexpr unsigned long kRemoteMaxS = 10;

  Sprayer(int pumpPin, AutoAim& aim);

  // Call first thing in setup(): forces the pump off.
  void begin();

  // Call from loop() every iteration, after autoAim.update().
  void update();

  // Encoder input stops the water.
  void notifyUserActivity();

  // A remote command arrived: stops an automatic run only, so a manual
  // run keeps going while the user aims.
  void notifyRemoteCommand();

  // Remote manual run (pumpOn / pumpOff). seconds is clamped to
  // 1..kRemoteMaxS.
  void remoteStart(int seconds);
  void remoteStop();

  // Handles "spray ..." and "pump ..." lines; returns false otherwise.
  bool handleCommand(const char* line);

  bool pumpOn() const { return run_ != Run::None; }

 private:
  enum class Run { None, Auto, Manual };

  void start(Run run, unsigned long maxMs, const char* why);
  void stop(const char* why);
  void setAuto(bool on);
  void printStatus();

  const int pin_;
  AutoAim& aim_;

  bool autoEnabled_ = kAutoAtBoot;
  bool announced_ = false;
  Run run_ = Run::None;
  unsigned long startMs_ = 0;
  unsigned long maxMs_ = 0;
  unsigned long cooldownUntilMs_ = 0;
  bool cooling_ = false;
  unsigned long autoRuns_ = 0;
};
