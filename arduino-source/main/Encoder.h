#pragma once

#include <Arduino.h>

// KY-040 quadrature encoder with a momentary SW pushbutton.
//
// CLK/DT are read via an ISR (display.display() blocks ~30 ms on I2C, so
// polling would miss ticks). Each detent moves the signed dial position by
// ±1, clamped to ±range. The SW button has a short-press behavior (zero
// the dial immediately) handled internally, and a long-press event the
// caller consumes via consumeLongPress() (typically to trigger a re-home).
class Encoder {
 public:
  static constexpr int kRange = 20;
  static constexpr unsigned long kIsrDebounceUs = 1500;
  static constexpr unsigned long kSwDebounceMs = 50;
  static constexpr unsigned long kLongPressMs = 1500;

  Encoder(int swPin, int dtPin, int clkPin);

  void begin();
  void update();                    // call from loop()
  int position() const;             // current signed dial value
  void reset();                     // zero the dial (interrupt-safe)

  // Set the dial to an arbitrary value (clamped to ±kRange). Used by
  // the remote setSliderSpeed command in All Motors mode so a website
  // click can re-anchor the encoder-driven speed setpoint. The next
  // ISR tick will adjust from this new baseline, giving last-write-wins
  // semantics between the physical dial and the remote.
  void set(int value);

  // Consume the dial position as a delta: returns the current value and
  // zeros it atomically. Use this when the encoder is driving discrete
  // navigation (menu scrolling) rather than an absolute speed setpoint.
  int consumeDelta();

  // Pause/resume the CLK ISR. Use around blocking operations like
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
  static void IRAM_ATTR clkIsrTrampoline();
  void IRAM_ATTR onClk();

  int swPin_;
  int dtPin_;
  int clkPin_;

  volatile int position_;
  volatile unsigned long lastIsrUs_;

  int swState_;
  int lastSwReading_;
  unsigned long lastDebounceMs_;
  unsigned long pressStartMs_;
  bool longPressFired_;
  bool longPressEvent_;
  bool shortPressEvent_;
};

static_assert(Encoder::kRange > 0, "encoder range must be positive");
