#pragma once

#include <Arduino.h>

class SliderMotor1;
class PanMotor2;
class TiltMotor3;
class Encoder;
class LimitSwitch;

// Background FreeRTOS task that POSTs current motor + UI state to
// https://<host>/api/motor/state. Heartbeat every kHeartbeatMs;
// immediate push when mode / menuIndex / limitEngaged / sliderHomed
// change (rate-limited to kMinIntervalMs so rapid toggles don't flood
// the worker).
//
// Pinned to core 0 so HTTPS work (TLS handshake, blocking socket I/O)
// stays off the core that runs the motor pulse timers and encoder ISR.
// Read access to shared state is via atomic single-word loads, so no
// main-loop synchronization is required.
class StatePusher {
 public:
  static constexpr unsigned long kHeartbeatMs = 1000;
  static constexpr unsigned long kMinIntervalMs = 200;
  static constexpr unsigned long kHttpTimeoutMs = 5000;
  static constexpr int kStackBytes = 12288;
  static constexpr UBaseType_t kPriority = 1;
  static constexpr BaseType_t kCore = 0;
  static constexpr unsigned long kPollIntervalMs = 50;

  StatePusher(SliderMotor1& slider, PanMotor2& pan, TiltMotor3& tilt,
              Encoder& enc, LimitSwitch& limit);

  // Spin up the background task. host is the bare hostname, no scheme
  // (e.g. "embersensor.com").
  void begin(const char* host);

 private:
  static void taskTrampoline(void* arg);
  void run();
  void pushOnce();

  SliderMotor1& slider_;
  PanMotor2& pan_;
  TiltMotor3& tilt_;
  Encoder& enc_;
  LimitSwitch& limit_;

  const char* host_;

  // Last-pushed snapshot for change detection. Stored only on the task
  // thread so no synchronization needed.
  const char* lastModeName_;
  int lastMenuIndex_;
  bool lastLimitEngaged_;
  bool lastHomed_;
  unsigned long lastPushMs_;
};
