#include "display.h"
#include "pot.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
  const uint8_t SCREEN_WIDTH  = 128;
  const uint8_t SCREEN_HEIGHT = 64;
  const int8_t  OLED_RESET    = -1;
  const uint8_t OLED_ADDR     = 0x3C;

  Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

  // Cached state — update() only redraws when these change, so it is safe
  // to call from inside the motor's tight pulse loops.
  Mode lastMode = (Mode)-1;
  unsigned int lastDelay = 0;
  bool lastCW = true;
  bool lastPotDriving = false;
  unsigned long lastDrawMs = 0;
  const unsigned long DRAW_THROTTLE_MS = 100;

  const __FlashStringHelper* modeName(Mode m, bool potDriving) {
    if (potDriving) return F("POT");
    switch (m) {
      case MODE_DANCE:  return F("DANCE");
      case MODE_SMOOTH: return F("SMOOTH");
      case MODE_IDLE:   return F("IDLE");
    }
    return F("?");
  }
}

bool Display::begin() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 init failed - check wiring / I2C address"));
    return false;
  }
  return true;
}

void Display::showBanner() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.println(F("Stepper"));
  oled.setTextSize(1);
  oled.setCursor(0, 24);
  oled.println(F("d=dance  r=smooth"));
  oled.setCursor(0, 36);
  oled.println(F("f=faster s=stop"));
  oled.display();
  delay(1500);
}

void Display::update(Mode mode, unsigned int smoothDelay, bool cw) {
  // When idle and the pot is deflected, render as a "POT" screen with the
  // pot's live direction and speed; otherwise show whatever the caller passed.
  bool potDriving = (mode == MODE_IDLE) && Pot::isActive();
  unsigned int delayShown = potDriving ? Pot::speedDelayUs() : smoothDelay;
  bool cwShown            = potDriving ? Pot::isCW()         : cw;

  if (mode == lastMode && delayShown == lastDelay && cwShown == lastCW
      && potDriving == lastPotDriving) return;

  // Throttle redraws to avoid saturating I2C while the pot is being
  // continuously turned (mode/direction changes still bypass the throttle).
  bool stateChanged = (mode != lastMode) || (cwShown != lastCW)
                      || (potDriving != lastPotDriving);
  unsigned long now = millis();
  if (!stateChanged && (now - lastDrawMs) < DRAW_THROTTLE_MS) return;

  lastMode        = mode;
  lastDelay       = delayShown;
  lastCW          = cwShown;
  lastPotDriving  = potDriving;
  lastDrawMs      = now;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.print(modeName(mode, potDriving));

  bool showSpeedUI = (mode == MODE_SMOOTH) || potDriving;
  if (showSpeedUI) {
    oled.setTextSize(1);
    oled.setCursor(80, 4);
    oled.print(cwShown ? F("CW") : F("CCW"));

    oled.setCursor(0, 24);
    oled.print(F("delay: "));
    oled.print(delayShown);
    oled.println(F(" us"));

    // Speed bar — shorter delay = longer bar.
    const long MIN_D = 150;
    const long MAX_D = 2000;
    long w = (long)(MAX_D - (long)delayShown) * 128 / (MAX_D - MIN_D);
    if (w < 0) w = 0;
    if (w > 128) w = 128;
    oled.fillRect(0, 40, w, 8, SSD1306_WHITE);
    oled.drawRect(0, 40, 128, 8, SSD1306_WHITE);
  } else if (mode == MODE_IDLE) {
    oled.setTextSize(1);
    oled.setCursor(0, 24);
    oled.println(F("d=dance"));
    oled.setCursor(0, 36);
    oled.println(F("r=run smooth"));
  }

  oled.display();
}
