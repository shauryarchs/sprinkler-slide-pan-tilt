#include "Display.h"

namespace {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
}  // namespace

Display::Display(uint8_t addr)
    : display_(kScreenWidth, kScreenHeight, &Wire, -1),
      addr_(addr),
      ok_(false) {}

bool Display::begin() {
  ok_ = display_.begin(SSD1306_SWITCHCAPVCC, addr_);
  if (ok_) {
    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);
  }
  return ok_;
}

void Display::showHomingMessage() {
  if (!ok_) return;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(2);
  display_.setCursor(0, 0);
  display_.println(F("Homing..."));
  display_.setTextSize(1);
  display_.setCursor(0, 22);
  display_.println(F("Please wait,"));
  display_.setCursor(0, 34);
  display_.println(F("don't press any"));
  display_.setCursor(0, 46);
  display_.println(F("buttons or dials"));
  display_.display();
}

void Display::showMenu(int selection) {
  if (!ok_) return;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  display_.setTextSize(1);
  display_.setCursor(0, 0);
  display_.println(F("Select Motor:"));

  // Size-2 menu items (12x16 px) — at y=48 the last item ends at exactly
  // y=64, the bottom of the panel.
  display_.setTextSize(2);
  display_.setCursor(0, 12);
  display_.print(selection == 0 ? F("> Motor 1") : F("  Motor 1"));
  display_.setCursor(0, 30);
  display_.print(selection == 1 ? F("> Motor 2") : F("  Motor 2"));
  display_.setCursor(0, 48);
  display_.print(selection == 2 ? F("> Motor 3") : F("  Motor 3"));

  display_.display();
}

void Display::showMotor1Status(int dial, long posMm, bool limitEngaged) {
  if (!ok_) return;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(1);

  display_.setCursor(0, 0);
  display_.println(F("Ember Sensor"));

  display_.setCursor(0, 10);
  display_.println(F("www.embersensor.com"));

  display_.setCursor(0, 22);
  display_.print(F("Dial: "));
  if (dial > 0) display_.print('+');
  display_.print(dial);
  display_.print(' ');
  if (dial > 0) display_.print(F("CW"));
  else if (dial < 0) display_.print(F("CCW"));
  else display_.print(F("STOP"));

  display_.setCursor(0, 33);
  display_.print(F("Pos: "));
  display_.print(posMm);
  display_.print(F(" mm"));

  display_.setCursor(0, 44);
  display_.print(F("Limit: "));
  display_.print(limitEngaged ? F("LOW (hit)") : F("HIGH"));

  display_.setCursor(0, 56);
  display_.print(F("By, Shaurya Varshnay"));

  display_.display();
}

void Display::showMotor2Status(int dial, long posDeg) {
  if (!ok_) return;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(1);

  display_.setCursor(0, 0);
  display_.println(F("Ember Sensor"));

  display_.setCursor(0, 10);
  display_.println(F("www.embersensor.com"));

  display_.setCursor(0, 22);
  display_.print(F("Dial: "));
  if (dial > 0) display_.print('+');
  display_.print(dial);
  display_.print(' ');
  if (dial > 0) display_.print(F("CW"));
  else if (dial < 0) display_.print(F("CCW"));
  else display_.print(F("STOP"));

  display_.setCursor(0, 33);
  display_.print(F("Pos: "));
  display_.print(posDeg);
  display_.print(F(" deg"));

  display_.setCursor(0, 44);
  display_.print(F("Motor 2 [0-180]"));

  display_.setCursor(0, 56);
  display_.print(F("By, Shaurya Varshnay"));

  display_.display();
}

void Display::showMotor3Status(int dial, long posDeg) {
  if (!ok_) return;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(1);

  display_.setCursor(0, 0);
  display_.println(F("Ember Sensor"));

  display_.setCursor(0, 10);
  display_.println(F("www.embersensor.com"));

  display_.setCursor(0, 22);
  display_.print(F("Dial: "));
  if (dial > 0) display_.print('+');
  display_.print(dial);
  display_.print(' ');
  if (dial > 0) display_.print(F("CW"));
  else if (dial < 0) display_.print(F("CCW"));
  else display_.print(F("STOP"));

  display_.setCursor(0, 33);
  display_.print(F("Pos: "));
  display_.print(posDeg);
  display_.print(F(" deg"));

  display_.setCursor(0, 44);
  display_.print(F("Motor 3 [0-180]"));

  display_.setCursor(0, 56);
  display_.print(F("By, Shaurya Varshnay"));

  display_.display();
}
