// Stepper + OLED — main entry point.
// Wires together the Motor, Display, and Buttons modules and parses serial commands.
//
// Serial Monitor (9600 baud):
//   'r' -> smooth clockwise spin
//   'f' -> double smooth speed (halves half-period)
//   's' -> stop
//
// Buttons (tap to toggle; latched until stop / opposite direction):
//   D4 (blue)  -> spin CCW (left)
//   D5 (green) -> spin CW (right)
//   D6 (red)   -> stop
//   Wired button-to-GND, internal INPUT_PULLUP enabled.
//
// Wiring (Arduino Uno):
//   A4988 STEP -> D3   A4988 DIR  -> D2   (motor power separate on VMOT/GND)
//   OLED  SDA  -> A4   OLED  SCL  -> A5   OLED VCC -> 3.3V   OLED GND -> GND

#include "motor.h"
#include "display.h"
#include "buttons.h"

void pollSerial() {
  if (Serial.available() <= 0) return;
  char c = Serial.read();
  switch (c) {
    case 'r': case 'R':
      Motor::startSmooth(true);     Serial.println(F("smooth CW"));  break;
    case 'f': case 'F':
      Motor::faster(); break;
    case 's': case 'S':
      Motor::stopPressed();         Serial.println(F("stopped")); break;
  }
}

void pollButtons() {
  switch (Buttons::pollEvent()) {
    case Buttons::EVENT_LEFT:
      Motor::startSmooth(false);    Serial.println(F("btn: CCW")); break;
    case Buttons::EVENT_RIGHT:
      Motor::startSmooth(true);     Serial.println(F("btn: CW"));  break;
    case Buttons::EVENT_STOP:
      Motor::stopPressed();         Serial.println(F("btn: stop")); break;
    case Buttons::EVENT_NONE:
      break;
  }
}

// Called from inside Motor's blocking step loops. Display::update() is a
// no-op when state is unchanged, so the I2C cost is paid once per
// mode/speed change rather than every pulse.
void tick() {
  pollSerial();
  pollButtons();
  Display::update(Motor::getMode(), Motor::getSmoothDelay(), Motor::isSmoothCW());
}

void setup() {
  Serial.begin(9600);
  Motor::begin();
  Buttons::begin();
  if (!Display::begin()) {
    Serial.println(F("continuing without display"));
  } else {
    Display::showBanner();
  }
  Serial.println(F("Send 'r' for smooth, 'f' to double speed, 's' to stop."));
  Serial.println(F("Or use buttons: D4=CCW, D5=CW, D6=stop."));
}

void loop() {
  tick();
  Motor::update();
}
