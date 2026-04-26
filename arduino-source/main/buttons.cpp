#include "buttons.h"

const uint8_t LEFT_PIN  = 4;
const uint8_t RIGHT_PIN = 5;
const uint8_t STOP_PIN  = 6;

const unsigned long DEBOUNCE_MS = 30;

namespace {
  struct Btn {
    uint8_t pin;
    uint8_t lastReading;     // most recent raw read
    uint8_t stableState;     // last debounced state
    unsigned long changeMs;  // when lastReading last flipped
    Buttons::Event event;
  };

  Btn btns[3] = {
    { LEFT_PIN,  HIGH, HIGH, 0, Buttons::EVENT_LEFT  },
    { RIGHT_PIN, HIGH, HIGH, 0, Buttons::EVENT_RIGHT },
    { STOP_PIN,  HIGH, HIGH, 0, Buttons::EVENT_STOP  },
  };
}

void Buttons::begin() {
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(btns[i].pin, INPUT_PULLUP);
  }
  // Let the pull-ups settle, then snapshot the resting pin state. Any pin
  // that happens to read LOW at boot (held button, miswire, NC switch)
  // would otherwise emit a phantom press event after the debounce window.
  delayMicroseconds(10);
  unsigned long now = millis();
  Serial.print(F("btn init:"));
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t r = digitalRead(btns[i].pin);
    btns[i].lastReading = r;
    btns[i].stableState = r;
    btns[i].changeMs = now;
    Serial.print(F(" D"));
    Serial.print(btns[i].pin);
    Serial.print('=');
    Serial.print(r == LOW ? F("LOW") : F("HIGH"));
  }
  Serial.println();
}

Buttons::Event Buttons::pollEvent() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t r = digitalRead(btns[i].pin);
    if (r != btns[i].lastReading) {
      btns[i].lastReading = r;
      btns[i].changeMs = now;
    }
    if ((now - btns[i].changeMs) > DEBOUNCE_MS && r != btns[i].stableState) {
      btns[i].stableState = r;
      if (r == LOW) return btns[i].event;   // press edge (pull-up: pressed = LOW)
    }
  }
  return EVENT_NONE;
}
