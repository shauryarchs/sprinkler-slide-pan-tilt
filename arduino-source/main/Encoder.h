#pragma once

#include <Arduino.h>

// KY-040 quadrature encoder with a momentary SW pushbutton.
//
// CLK/DT are read via ISRs (display.display() blocks ~30 ms on I2C, so
// polling would miss ticks). Both pins interrupt on every change and a
// quadrature state machine follows the full CLK/DT sequence, ignoring
// impossible jumps caused by contact bounce. Each detent moves the signed
// dial position by ±1, clamped to ±range. The SW button has a short-press
// behavior (zero the dial immediately) handled internally, and a long-press
// event the caller consumes via consumeLongPress() (typically to trigger a
// re-home).
class Encoder {
 public:
  static constexpr int kRange = 20;
  // Valid CLK/DT transitions per detent (click). Most KY-040 modules go
  // through the full 4-step sequence per click and rest with CLK and DT
  // both HIGH. Set to 2 for modules that rest at both HIGH/HIGH and
  // LOW/LOW (half a cycle per click); those count only every other click
  // when this is 4.
  static constexpr int kTransitionsPerDetent = 4;
  static constexpr unsigned long kSwDebounceMs = 50;
  static constexpr unsigned long kLongPressMs = 1500;

  Encoder(int swPin, int dtPin, int clkPin);

  void begin();
  void update();                    // call from loop()
  int position() const;             // current signed dial value
  void reset();                     // zero the dial (interrupt-safe)

  // Set the dial to an arbitrary value (clamped to ±kRange). Used by
  // the remote setSliderSpeed command in any motor mode (Motor 1/2/3
  // or Patrol) so a website slider can re-anchor the encoder-driven
  // speed setpoint. The next ISR tick will adjust from this new
  // baseline, giving last-write-wins semantics between the physical
  // dial and the remote.
  void set(int value);

  // Consume the dial position as a delta: returns the current value and
  // zeros it atomically. Use this when the encoder is driving discrete
  // navigation (menu scrolling) rather than an absolute speed setpoint.
  int consumeDelta();

  // Pause/resume the CLK/DT ISRs. Use around blocking operations like
  // homing where dial spins should be ignored.
  void suspend();
  void resume();

  // After suspend()/long blocking work, syncs the SW state machine to
  // the live pin state so a hold-through-resume doesn't register as a
  // new falling edge or fire an immediate long-press.
  void syncSwState();

  // One-shot SW events. consumeShortPress() fires on release if the
  // button was held for less than kLongPressMs ("tap"). consumeLongPress()
  // fires once while the button is still held, when the hold crosses
  // kLongPressMs. They're mutually exclusive for a given press cycle.
  bool consumeShortPress();
  bool consumeLongPress();

 private:
  static Encoder* instance_;
  static void IRAM_ATTR isrTrampoline();
  void IRAM_ATTR onEdge();
  int readState() const;

  int swPin_;
  int dtPin_;
  int clkPin_;

  volatile int position_;
  volatile uint8_t quadState_;  // last (CLK << 1) | DT
  volatile int8_t subSteps_;    // valid transitions since the last rest

  int swState_;
  int lastSwReading_;
  unsigned long lastDebounceMs_;
  unsigned long pressStartMs_;
  bool longPressFired_;
  bool longPressEvent_;
  bool shortPressEvent_;
};

static_assert(Encoder::kRange > 0, "encoder range must be positive");
static_assert(Encoder::kTransitionsPerDetent == 2 ||
                  Encoder::kTransitionsPerDetent == 4,
              "encoder detents are 2 or 4 transitions");
