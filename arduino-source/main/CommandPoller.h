#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "AppState.h"

// Background FreeRTOS task that polls
// https://<host>/api/motor/command?since=<lastSeqAck> every kPollMs.
// On a new command (seq > lastSeqAck) the parsed command is pushed
// onto a queue that main loop drains and dispatches into the existing
// enterMenu / enterMotor1 / ... / rehome / zeroPan / zeroTilt / set-
// SliderSpeed flows.
//
// The reason for the queue: enterMotorX() and friends touch globals
// (mode, encoder state, lastDisplayUpdateMs) and call non-reentrant
// Arduino APIs (e.g. detachInterrupt during rehome). Doing that from
// the network task on core 0 while the main loop on core 1 is mid-
// transition is asking for races. Letting the main loop pull commands
// off a queue keeps the dispatch single-threaded.
//
// Pinned to core 0, same as StatePusher.

enum class RemoteCmdKind : uint8_t {
  None,
  EnterMode,
  Rehome,
  ZeroPan,
  ZeroTilt,
  SetSliderSpeed,
  NudgePan,
  NudgeTilt,
};

struct RemoteCommand {
  RemoteCmdKind kind;
  Mode targetMode;   // for EnterMode
  int speed;         // for SetSliderSpeed (clamped ±Encoder::kRange)
  int deltaDeg;      // for NudgePan / NudgeTilt (signed degrees, ±360)
  unsigned long seq; // from worker — bumped into lastSeqAck after dispatch
};

class CommandPoller {
 public:
  static constexpr unsigned long kPollMs = 1500;
  static constexpr unsigned long kHttpTimeoutMs = 5000;
  static constexpr int kStackBytes = 16384;  // ArduinoJson + HTTPClient = big stacks
  static constexpr UBaseType_t kPriority = 1;
  static constexpr BaseType_t kCore = 0;
  static constexpr UBaseType_t kQueueDepth = 4;

  CommandPoller();

  // Returns the shared command queue. Created in begin().
  QueueHandle_t queue() const { return queue_; }

  // Spin up the queue + background task. host is a bare hostname, no
  // scheme (StatePusher / CommandPoller both prepend "https://").
  void begin(const char* host);

 private:
  static void taskTrampoline(void* arg);
  void run();
  void pollOnce();
  bool parseCommandJson(const char* json, RemoteCommand* out);

  const char* host_;
  QueueHandle_t queue_;
};
