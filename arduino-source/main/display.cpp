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
  int  lastFrame = -1;
  unsigned long lastDrawMs = 0;
  const unsigned long DRAW_THROTTLE_MS = 100;
  const unsigned long FRAME_MS = 150;       // ~6.7 fps running-dog animation

  void drawRunningDog(int frame) {
    oled.setTextSize(1);
    oled.setCursor(0, 24);
    // Head — same in all four frames.
    oled.println(F("   / \\__"));
    oled.println(F("  (    @\\___"));
    oled.println(F(" /          O"));
    // Legs — animate.
    switch (frame & 3) {
      case 0:
        oled.println(F("/   /--\\   /"));
        oled.println(F("\\__/    \\_/"));
        break;
      case 1:
        oled.println(F("/   /\\     /"));
        oled.println(F("\\__/  \\___/"));
        break;
      case 2:
        oled.println(F("/    /---\\"));
        oled.println(F("\\___/    /"));
        break;
      case 3:
        oled.println(F("/   /--\\   \\"));
        oled.println(F("\\__/    \\__\\"));
        break;
    }
  }

  const __FlashStringHelper* modeName(Mode m, bool potDriving, bool potOff) {
    if (potDriving) return F("POT");
    switch (m) {
      case MODE_DANCE:  return F("DANCE");
      case MODE_SMOOTH: return F("SMOOTH");
      case MODE_IDLE:   return potOff ? F("STOP") : F("THEO");
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
  // Pot is a position encoder when motor is IDLE; "active" = user has
  // turned it within ACTIVE_TIMEOUT_MS regardless of position. The STOP
  // latch (Motor::isPotDisabled) suppresses pot tracking entirely.
  bool potOff    = Motor::isPotDisabled();
  bool potActive = (mode == MODE_IDLE) && !potOff && Pot::isActive();
  // Animate the running dog whenever the motor is rotating: continuous
  // smooth, dance routine, or pot-driven manual positioning.
  bool animating = (mode == MODE_SMOOTH) || (mode == MODE_DANCE) || potActive;
  int  frame     = animating ? (int)((millis() / FRAME_MS) & 3) : 0;

  if (mode == lastMode && smoothDelay == lastDelay && cw == lastCW
      && potActive == lastPotDriving && frame == lastFrame) return;

  // Throttle redraws so I2C doesn't saturate; state and animation-frame
  // changes both bypass the throttle so the dog keeps stepping in time.
  bool stateChanged = (mode != lastMode) || (cw != lastCW)
                      || (potActive != lastPotDriving);
  bool frameChanged = (frame != lastFrame);
  unsigned long now = millis();
  if (!stateChanged && !frameChanged && (now - lastDrawMs) < DRAW_THROTTLE_MS) return;

  lastMode       = mode;
  lastDelay      = smoothDelay;
  lastCW         = cw;
  lastPotDriving = potActive;
  lastFrame      = frame;
  lastDrawMs     = now;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.print(modeName(mode, potActive, potOff));

  // Direction badge in the top-right corner — only meaningful for SMOOTH.
  if (mode == MODE_SMOOTH) {
    oled.setTextSize(1);
    oled.setCursor(80, 4);
    oled.print(cw ? F("CW") : F("CCW"));
  }

  if (animating) {
    drawRunningDog(frame);
  } else if (mode == MODE_IDLE) {
    // Static dog at rest.
    oled.setTextSize(1);
    oled.setCursor(0, 24);
    oled.println(F("   / \\__"));
    oled.println(F("  (    @\\___"));
    oled.println(F("  /         O"));
    oled.println(F(" /   (_____/"));
    oled.println(F("/_____/   U"));
  }

  oled.display();
}
