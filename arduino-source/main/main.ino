#include <Wire.h>

#include "Display.h"
#include "Encoder.h"
#include "LimitSwitch.h"
#include "Motor2.h"
#include "Stepper.h"

// Pin assignments use Arduino Nano ESP32 silkscreen labels.
// The core resolves them to the right ESP32-S3 GPIOs.
namespace pins {
const int kEncoderSw = D3;
const int kEncoderDt = D4;
const int kEncoderClk = D5;
const int kMotor1Dir = D6;
const int kMotor1Step = D7;
const int kMotor2Dir = D8;
const int kMotor2Step = D9;
const int kLimit = D12;
}  // namespace pins

// display.display() blocks ~30 ms on I2C; refreshing every 250 ms keeps
// the UI readable while limiting stepping duty loss to ~12%.
const unsigned long kDisplayIntervalMs = 250;

Encoder encoder(pins::kEncoderSw, pins::kEncoderDt, pins::kEncoderClk);
LimitSwitch limitSwitch(pins::kLimit);
Stepper motor1(pins::kMotor1Dir, pins::kMotor1Step);
Motor2 motor2(pins::kMotor2Dir, pins::kMotor2Step);
Display oled;

// UI state machine: the menu is the resting screen; selecting an item
// hands the encoder over to that motor's control logic. Short-press
// returns to the menu (and stops the active motor).
enum class Mode {
  Menu,
  Motor1Control,
  Motor2Control,
};
Mode mode = Mode::Menu;
int menuIndex = 0;
const int kMenuItemCount = 2;

unsigned long lastDisplayUpdateMs = 0;

void enterMenu() {
  motor1.stop();
  motor2.stop();
  encoder.reset();
  encoder.syncSwState();  // suppress long-press from the press that got us here
  mode = Mode::Menu;
  lastDisplayUpdateMs = 0;
}

void enterMotor1() {
  encoder.reset();
  encoder.syncSwState();
  mode = Mode::Motor1Control;
  lastDisplayUpdateMs = 0;
}

void enterMotor2() {
  encoder.reset();
  encoder.syncSwState();
  mode = Mode::Motor2Control;
  lastDisplayUpdateMs = 0;
}

void rehome() {
  // Long-press in Motor1 mode re-homes. Drop the encoder ISR for the
  // duration of homing, then resync after.
  encoder.suspend();
  oled.showHomingMessage();
  motor1.home(limitSwitch);
  encoder.reset();
  encoder.resume();
  encoder.syncSwState();
  lastDisplayUpdateMs = 0;
}

void setup() {
  limitSwitch.begin();
  motor1.begin();
  motor2.begin();

  Wire.begin();
  Wire.setClock(400000);
  oled.begin();

  // Motor1 boot homing; motor2 has no limit switch so we just trust
  // its mechanical alignment at power-on.
  oled.showHomingMessage();
  motor1.home(limitSwitch);

  encoder.begin();
  encoder.syncSwState();

  mode = Mode::Menu;
}

void handleMenu() {
  int delta = encoder.consumeDelta();
  if (delta != 0) {
    int step = (delta > 0) ? 1 : -1;
    menuIndex = (menuIndex + step + kMenuItemCount) % kMenuItemCount;
  }
  if (encoder.consumeShortPress()) {
    if (menuIndex == 0) enterMotor1();
    else enterMotor2();
  }
  // Long-press has no meaning in the menu — drain it so it doesn't
  // fire later in motor mode if the user held through the transition.
  encoder.consumeLongPress();
}

void handleMotor1() {
  if (encoder.consumeShortPress()) {
    enterMenu();
    return;
  }
  if (encoder.consumeLongPress()) {
    rehome();
    return;
  }

  int dial = encoder.position();
  motor1.update(dial, limitSwitch);

  // Auto-reset the dial when pinned at an end of travel so the next
  // click in the opposite direction goes straight into reverse.
  long posSteps = motor1.positionSteps();
  if (dial > 0 && posSteps >= Stepper::kMaxPositionSteps) {
    encoder.reset();
  } else if (dial < 0 && posSteps <= Stepper::kMinPositionSteps) {
    encoder.reset();
  }
}

void handleMotor2() {
  if (encoder.consumeShortPress()) {
    enterMenu();
    return;
  }
  // Long-press is meaningful only on motor1 — drain it.
  encoder.consumeLongPress();

  int dial = encoder.position();
  motor2.update(dial);

  // Same auto-reset idea: at either end of the 0-180° range, snap the
  // dial back to 0 so reversing is one click away.
  long posSteps = motor2.positionSteps();
  if (dial > 0 && posSteps >= Motor2::kMaxPositionSteps) {
    encoder.reset();
  } else if (dial < 0 && posSteps <= 0) {
    encoder.reset();
  }
}

void loop() {
  encoder.update();

  switch (mode) {
    case Mode::Menu:          handleMenu();   break;
    case Mode::Motor1Control: handleMotor1(); break;
    case Mode::Motor2Control: handleMotor2(); break;
  }

  if (millis() - lastDisplayUpdateMs >= kDisplayIntervalMs) {
    switch (mode) {
      case Mode::Menu:
        oled.showMenu(menuIndex);
        break;
      case Mode::Motor1Control:
        oled.showMotor1Status(encoder.position(), motor1.positionMm(),
                              limitSwitch.engaged());
        break;
      case Mode::Motor2Control:
        oled.showMotor2Status(encoder.position(), motor2.positionDegrees());
        break;
    }
    lastDisplayUpdateMs = millis();
  }
}
