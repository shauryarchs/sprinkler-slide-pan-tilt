// Stepper + OLED — main entry point.
// Wires together the Motor and Display modules and parses serial commands.
//
// Serial Monitor (9600 baud):
//   'd' -> dance routine
//   'r' -> smooth clockwise spin
//   'f' -> double smooth speed (halves half-period)
//   's' -> stop
//
// Wiring (Arduino Uno):
//   A4988 STEP -> D3   A4988 DIR  -> D2   (motor power separate on VMOT/GND)
//   OLED  SDA  -> A4   OLED  SCL  -> A5   OLED VCC -> 5V   OLED GND -> GND

#include "motor.h"
#include "display.h"

void pollSerial() {
  if (Serial.available() <= 0) return;
  char c = Serial.read();
  switch (c) {
    case 'd': case 'D':
      Motor::setMode(MODE_DANCE);  Serial.println(F("dancing")); break;
    case 'r': case 'R':
      Motor::setMode(MODE_SMOOTH); Serial.println(F("smooth"));  break;
    case 'f': case 'F':
      Motor::faster(); break;
    case 's': case 'S':
      Motor::setMode(MODE_IDLE);   Serial.println(F("stopped")); break;
  }
}

// Called from inside Motor's blocking step loops. Display::update() is a
// no-op when state is unchanged, so the I2C cost is paid once per
// mode/speed change rather than every pulse.
void tick() {
  pollSerial();
  Display::update(Motor::getMode(), Motor::getSmoothDelay());
}

void setup() {
  Serial.begin(9600);
  Motor::begin();
  if (!Display::begin()) {
    Serial.println(F("continuing without display"));
  } else {
    Display::showBanner();
  }
  Serial.println(F("Send 'd' to dance, 'r' for smooth, 'f' to double speed, 's' to stop."));
}

void loop() {
  tick();
  Motor::update();
}
