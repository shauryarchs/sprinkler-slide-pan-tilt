// Stepper + OLED — main entry point.
// Wires together the Motor, Display, and Buttons modules and parses
// serial commands.
//
// Serial Monitor (9600 baud):
//   'r' -> smooth clockwise spin
//   'f' -> halve step interval (doubles speed, capped)
//   's' -> stop
//
// Buttons:
//   D4 (blue)  -> spin CCW (left)
//   D5 (green) -> spin CW (right)
//   D6 (red)   -> stop
//   Wired button-to-GND, internal INPUT_PULLUP enabled.
//
// Joystick (Y-axis only, used like a continuous CW/CCW/STOP button):
//   VRy -> A0    VCC -> 5V    GND -> GND
//   Push up    -> CW  spin (mirrors D5)
//   Push down  -> CCW spin (mirrors D4)
//   Recenter   -> stop
//   Swap EVENT_UP / EVENT_DOWN in pollJoystick() if it feels reversed.
//
// Wiring (Arduino Uno):
//   TB6600 PUL+ -> D3   TB6600 DIR+ -> D2   PUL-/DIR- -> GND
//   OLED  SDA   -> A4   OLED  SCL   -> A5   OLED VCC  -> 3.3V   OLED GND -> GND

#include "motor.h"
#include "display.h"
#include "buttons.h"
#include "joystick.h"

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

void pollJoystick() {
  // Direction transitions: start/stop the motor, same as buttons.
  switch (Joystick::poll()) {
    case Joystick::EVENT_UP:
      Motor::startSmooth(true);     Serial.println(F("joy: CW"));  break;
    case Joystick::EVENT_DOWN:
      Motor::startSmooth(false);    Serial.println(F("joy: CCW")); break;
    case Joystick::EVENT_CENTER:
      Motor::stopPressed();         Serial.println(F("joy: stop")); break;
    case Joystick::EVENT_NONE:
      break;
  }
  // Continuous speed control: more deflection = faster. Only override speed
  // while the stick is actually deflected, so 'f' / button-driven speeds
  // aren't fought when the joystick is centered.
  float defl = Joystick::deflection();
  if (defl > 0.0) Motor::setSpeedFraction(defl);
}

void setup() {
  Serial.begin(9600);
  Motor::begin();
  Buttons::begin();
  Joystick::begin();
  if (!Display::begin()) {
    Serial.println(F("continuing without display"));
  } else {
    Display::showBanner();
  }
  Serial.println(F("Send 'r' for smooth, 'f' to double speed, 's' to stop."));
  Serial.println(F("Or use buttons: D4=CCW, D5=CW, D6=stop."));
}

// Called from inside Motor's blocking step loop. Display::update() is a
// no-op when state is unchanged, so the I2C cost is paid once per
// mode/speed change rather than every batch of pulses.
void tick() {
  pollSerial();
  pollButtons();
  pollJoystick();
  Display::update(Motor::getMode(), Motor::getSmoothDelay(), Motor::isSmoothCW());
}

void loop() {
  tick();
  Motor::update();
}
