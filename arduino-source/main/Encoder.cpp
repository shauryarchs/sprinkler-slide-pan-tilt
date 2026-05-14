#include "Encoder.h"

Encoder* Encoder::instance_ = nullptr;

Encoder::Encoder(int swPin, int dtPin, int clkPin)
    : swPin_(swPin),
      dtPin_(dtPin),
      clkPin_(clkPin),
      position_(0),
      lastIsrUs_(0),
      swState_(HIGH),
      lastSwReading_(HIGH),
      lastDebounceMs_(0),
      pressStartMs_(0),
      longPressFired_(false),
      longPressEvent_(false) {}

void Encoder::begin() {
  pinMode(swPin_, INPUT_PULLUP);
  pinMode(dtPin_, INPUT_PULLUP);
  pinMode(clkPin_, INPUT_PULLUP);
  instance_ = this;
  resume();
}

void Encoder::suspend() {
  detachInterrupt(digitalPinToInterrupt(clkPin_));
}

void Encoder::resume() {
  attachInterrupt(digitalPinToInterrupt(clkPin_), clkIsrTrampoline, FALLING);
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
  interrupts();
}

void Encoder::update() {
  int reading = digitalRead(swPin_);
  if (reading != lastSwReading_) lastDebounceMs_ = millis();
  if ((millis() - lastDebounceMs_) > kSwDebounceMs) {
    if (reading != swState_) {
      swState_ = reading;
      if (swState_ == LOW) {
        // Falling edge: immediate stop. Re-arm the long-press window.
        reset();
        pressStartMs_ = millis();
        longPressFired_ = false;
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

void IRAM_ATTR Encoder::clkIsrTrampoline() {
  if (instance_) instance_->onClk();
}

void IRAM_ATTR Encoder::onClk() {
  unsigned long now = micros();
  if (now - lastIsrUs_ < kIsrDebounceUs) return;
  lastIsrUs_ = now;
  int dt = digitalRead(dtPin_);
  if (dt == HIGH) {
    if (position_ < kRange) position_++;
  } else {
    if (position_ > -kRange) position_--;
  }
}
