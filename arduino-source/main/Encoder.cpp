#include "Encoder.h"

Encoder* Encoder::instance_ = nullptr;

namespace {
// Quadrature transition table, indexed by (previous state << 2) | new state,
// where a state is (CLK << 1) | DT. +1/-1 are valid single steps; 0 means
// no change or an impossible two-bit jump (bounce or a missed edge), which
// is ignored. CLK falling while DT is HIGH is +1, matching the original
// CLK-edge decoder's direction.
const int8_t kQuadTable[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};
}  // namespace

Encoder::Encoder(int swPin, int dtPin, int clkPin)
    : swPin_(swPin),
      dtPin_(dtPin),
      clkPin_(clkPin),
      position_(0),
      quadState_(0),
      subSteps_(0),
      swState_(HIGH),
      lastSwReading_(yesHIGH),
      lastDebounceMs_(0),
      pressStartMs_(0),
      longPressFired_(false),
      longPressEvent_(false),
      shortPressEvent_(false) {}

void Encoder::begin() {
  pinMode(swPin_, INPUT_PULLUP);
  pinMode(dtPin_, INPUT_PULLUP);
  pinMode(clkPin_, INPUT_PULLUP);
  instance_ = this;
  resume();
}

void Encoder::suspend() {
  detachInterrupt(digitalPinToInterrupt(clkPin_));
  detachInterrupt(digitalPinToInterrupt(dtPin_));
}

void Encoder::resume() {
  // Start from wherever the knob is now so turns made while suspended
  // don't count.
  quadState_ = readState();
  subSteps_ = 0;
  attachInterrupt(digitalPinToInterrupt(clkPin_), isrTrampoline, CHANGE);
  attachInterrupt(digitalPinToInterrupt(dtPin_), isrTrampoline, CHANGE);
}

int Encoder::readState() const {
  return (digitalRead(clkPin_) << 1) | digitalRead(dtPin_);
}

void Encoder::syncSwState() {
  swState_ = digitalRead(swPin_);
  lastSwReading_ = swState_;
  lastDebounceMs_ = millis();
  // If SW is held LOW at sync time, treat the long-press for this press
  // as already-fired so it doesn't trigger spuriously the moment the
  // loop catches up. The user must release and re-press to long-press.
  longPressFired_ = (swState_ == LOW);
}

int Encoder::position() const {
  return position_;
}

void Encoder::reset() {
  noInterrupts();
  position_ = 0;
  subSteps_ = 0;
  interrupts();
}

void Encoder::set(int value) {
  if (value > kRange) value = kRange;
  if (value < -kRange) value = -kRange;
  noInterrupts();
  position_ = value;
  subSteps_ = 0;
  interrupts();
}

void Encoder::update() {
  int reading = digitalRead(swPin_);
  if (reading != lastSwReading_) lastDebounceMs_ = millis();
  if ((millis() - lastDebounceMs_) > kSwDebounceMs) {
    if (reading != swState_) {
      swState_ = reading;
      if (swState_ == LOW) {
        // Falling edge: arm the long-press timer. Short-press doesn't
        // fire here — it fires on release below if we didn't cross
        // the long-press threshold. Otherwise a press intended as a
        // long hold would fire short-press first and the caller would
        // act on it before the hold completed.
        pressStartMs_ = millis();
        longPressFired_ = false;
      } else {
        // Rising edge: tap completed. If the long-press already fired
        // for this press cycle, swallow the release. Otherwise this
        // was a tap → short press.
        if (!longPressFired_) {
          shortPressEvent_ = true;
        }
      }
    }
    if (swState_ == LOW && !longPressFired_ &&
        (millis() - pressStartMs_) > kLongPressMs) {
      longPressFired_ = true;
      longPressEvent_ = true;
    }
  }
  lastSwReading_ = reading;
}

bool Encoder::consumeLongPress() {
  if (longPressEvent_) {
    longPressEvent_ = false;
    return true;
  }
  return false;
}

bool Encoder::consumeShortPress() {
  if (shortPressEvent_) {
    shortPressEvent_ = false;
    return true;
  }
  return false;
}

int Encoder::consumeDelta() {
  noInterrupts();
  int d = position_;
  position_ = 0;
  interrupts();
  return d;
}

void IRAM_ATTR Encoder::isrTrampoline() {
  if (instance_) instance_->onEdge();
}

// Called on every CLK or DT change. Follows the quadrature sequence and
// counts one step when the knob settles at a rest state (a detent) after
// moving at least half a detent in one direction. Bounce makes the state
// flip back and forth, adding and removing the same step, so it cancels
// out instead of being counted or reversing the direction.
void IRAM_ATTR Encoder::onEdge() {
  uint8_t state = (digitalRead(clkPin_) << 1) | digitalRead(dtPin_);
  int8_t step = kQuadTable[(quadState_ << 2) | state];
  quadState_ = state;
  if (step == 0) return;

  int8_t sub = subSteps_ + step;
  if (sub > kTransitionsPerDetent) sub = kTransitionsPerDetent;
  if (sub < -kTransitionsPerDetent) sub = -kTransitionsPerDetent;

  // Every module rests with CLK and DT HIGH (pull-ups); half-cycle
  // modules also rest with both LOW.
  bool atRest = state == 0b11 || (kTransitionsPerDetent == 2 && state == 0b00);
  if (atRest) {
    if (sub >= kTransitionsPerDetent / 2) {
      if (position_ < kRange) position_++;
    } else if (sub <= -kTransitionsPerDetent / 2) {
      if (position_ > -kRange) position_--;
    }
    sub = 0;
  }
  subSteps_ = sub;
}
