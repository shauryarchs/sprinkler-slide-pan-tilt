#include "pot.h"

const uint8_t POT_PIN          = A0;
const int     POT_CENTER       = 512;     // ADC midpoint at 5 V ref
const int     POT_DEADBAND     = 25;      // ~5 % each side of center
const unsigned int POT_MAX_DELAY_US = 2000; // slowest step rate
const unsigned int POT_MIN_DELAY_US = 150;  // fastest step rate
const unsigned long POT_CACHE_MS    = 5;    // refresh interval

namespace {
  unsigned long lastReadMs = 0;
  bool          primed     = false;
  int           cachedDelta = 0;

  void refresh() {
    unsigned long now = millis();
    if (!primed || now - lastReadMs >= POT_CACHE_MS) {
      cachedDelta = analogRead(POT_PIN) - POT_CENTER;
      lastReadMs = now;
      primed = true;
    }
  }
}

void Pot::begin() {
  cachedDelta = analogRead(POT_PIN) - POT_CENTER;
  lastReadMs  = millis();
  primed      = true;
}

bool Pot::isActive() {
  refresh();
  return abs(cachedDelta) > POT_DEADBAND;
}

bool Pot::isCW() {
  refresh();
  return cachedDelta > 0;
}

unsigned int Pot::speedDelayUs() {
  refresh();
  int absDelta = abs(cachedDelta);
  if (absDelta <= POT_DEADBAND) return POT_MAX_DELAY_US;
  unsigned int mag    = absDelta - POT_DEADBAND;
  unsigned int maxMag = 511 - POT_DEADBAND;
  if (mag > maxMag) mag = maxMag;
  return map(mag, 0, maxMag, POT_MAX_DELAY_US, POT_MIN_DELAY_US);
}
