#include <Wire.h>

#include "Display.h"
#include "Encoder.h"
#include "LimitSwitch.h"
#include "PanMotor2.h"
#include "SliderMotor1.h"
#include "TiltMotor3.h"

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
const int kMotor3Dir = D10;
const int kMotor3Step = D11;
const int kLimit = D12;
}  // namespace pins

// display.display() blocks the main loop for ~30 ms on the I2C frame
// transfer. With timer-driven stepping the motor's pulse stream is
// unaffected by that block, so we can refresh aggressively for snappy
// dial feedback (10 fps -> ~150 ms worst-case visual lag from a click).
const unsigned long kDisplayIntervalMs = 100;

Encoder encoder(pins::kEncoderSw, pins::kEncoderDt, pins::kEncoderClk);
LimitSwitch limitSwitch(pins::kLimit);
SliderMotor1 sliderMotor1(pins::kMotor1Dir, pins::kMotor1Step);
PanMotor2 panMotor2(pins::kMotor2Dir, pins::kMotor2Step);
TiltMotor3 tiltMotor3(pins::kMotor3Dir, pins::kMotor3Step);
Display oled;

// UI state machine: the menu is the resting screen; selecting an item
// hands the encoder over to that motor's control logic. Short-press
// returns to the menu (and stops the active motor).
enum class Mode {
  Menu,
  Motor1Control,
  Motor2Control,
  Motor3Control,
  AllMotorsControl,
};
Mode mode = Mode::Menu;
int menuIndex = 0;
const int kMenuItemCount = 4;

// "All motors" demo mode. Slider bounces between its soft floor and
// ceiling; Pan and Tilt rotate CW continuously. Speeds are fixed (the
// encoder dial is inert in this mode) — magnitudes are on the encoder's
// signed [-kEncoderRange, +kEncoderRange] scale.
const int kAllMotorsSliderSpeed = 15;
const int kAllMotorsPanSpeed = 12;
const int kAllMotorsTiltSpeed = 12;
// Wrap Pan/Tilt back below the soft ceiling this many steps early so
// the ISR's hard backstop never trips during a display.display() block.
const long kAllMotorsWrapMarginSteps = 200;
// Bounce state for the slider in all-motors mode. +1 = heading toward
// the soft ceiling, -1 = heading toward the soft floor.
int allMotorsSliderDir = 1;

unsigned long lastDisplayUpdateMs = 0;

void enterMenu() {
  sliderMotor1.stop();
  panMotor2.stop();
  tiltMotor3.stop();
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

void enterMotor3() {
  encoder.reset();
  encoder.syncSwState();
  mode = Mode::Motor3Control;
  lastDisplayUpdateMs = 0;
}

void enterAllMotors() {
  encoder.reset();
  encoder.syncSwState();
  // Pick a slider direction that doesn't stall against the ceiling on
  // entry. If the carriage already sits at the soft ceiling, start by
  // heading back; otherwise head out.
  if (sliderMotor1.positionSteps() >= SliderMotor1::kMaxPositionSteps) {
    allMotorsSliderDir = -1;
  } else {
    allMotorsSliderDir = 1;
  }
  mode = Mode::AllMotorsControl;
  lastDisplayUpdateMs = 0;
}

void rehome() {
  // Long-press in Motor1 mode re-homes. Drop the encoder ISR for the
  // duration of homing, then resync after.
  encoder.suspend();
  oled.showHomingMessage();
  sliderMotor1.home(limitSwitch);
  encoder.reset();
  encoder.resume();
  encoder.syncSwState();
  lastDisplayUpdateMs = 0;
}

void setup() {
  limitSwitch.begin();
  sliderMotor1.begin();
  panMotor2.begin();
  tiltMotor3.begin();

  Wire.begin();
  Wire.setClock(400000);
  oled.begin();

  // Motor1 boot homing; motor2 has no limit switch so we just trust
  // its mechanical alignment at power-on.
  oled.showHomingMessage();
  sliderMotor1.home(limitSwitch);

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
    switch (menuIndex) {
      case 0: enterMotor1(); break;
      case 1: enterMotor2(); break;
      case 2: enterMotor3(); break;
      case 3: enterAllMotors(); break;
    }
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
  sliderMotor1.update(dial, limitSwitch);

  // Auto-reset the dial when pinned at an end of travel so the next
  // click in the opposite direction goes straight into reverse.
  long posSteps = sliderMotor1.positionSteps();
  if (dial > 0 && posSteps >= SliderMotor1::kMaxPositionSteps) {
    encoder.reset();
  } else if (dial < 0 && posSteps <= SliderMotor1::kMinPositionSteps) {
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
  panMotor2.update(dial);

  // Same auto-reset idea: at either end of the 0-180° range, snap the
  // dial back to 0 so reversing is one click away.
  long posSteps = panMotor2.positionSteps();
  if (dial > 0 && posSteps >= PanMotor2::kMaxPositionSteps) {
    encoder.reset();
  } else if (dial < 0 && posSteps <= 0) {
    encoder.reset();
  }
}

void handleMotor3() {
  if (encoder.consumeShortPress()) {
    enterMenu();
    return;
  }
  encoder.consumeLongPress();

  int dial = encoder.position();
  tiltMotor3.update(dial);

  long posSteps = tiltMotor3.positionSteps();
  if (dial > 0 && posSteps >= TiltMotor3::kMaxPositionSteps) {
    encoder.reset();
  } else if (dial < 0 && posSteps <= 0) {
    encoder.reset();
  }
}

void handleAllMotors() {
  if (encoder.consumeShortPress()) {
    enterMenu();
    return;
  }
  // Encoder dial is inert in this mode — drain both button events so a
  // hold-through-entry doesn't fire later.
  encoder.consumeLongPress();

  // Slider: bounce. Flip direction once the carriage reaches either
  // soft bound; the motor's own update() will already have stopped it
  // there, and the next loop iteration will start it moving back.
  long sliderPos = sliderMotor1.positionSteps();
  if (allMotorsSliderDir > 0 && sliderPos >= SliderMotor1::kMaxPositionSteps) {
    allMotorsSliderDir = -1;
  } else if (allMotorsSliderDir < 0 && sliderPos <= SliderMotor1::kMinPositionSteps) {
    allMotorsSliderDir = 1;
  }
  sliderMotor1.update(allMotorsSliderDir * kAllMotorsSliderSpeed, limitSwitch);

  // Pan and Tilt: continuous CW. Wrap the step counter modulo the soft
  // ceiling so the ISR's bounds check keeps passing.
  panMotor2.update(kAllMotorsPanSpeed);
  if (panMotor2.positionSteps() >=
      PanMotor2::kMaxPositionSteps - kAllMotorsWrapMarginSteps) {
    panMotor2.wrapPosition();
  }
  tiltMotor3.update(kAllMotorsTiltSpeed);
  if (tiltMotor3.positionSteps() >=
      TiltMotor3::kMaxPositionSteps - kAllMotorsWrapMarginSteps) {
    tiltMotor3.wrapPosition();
  }
}

void loop() {
  encoder.update();

  switch (mode) {
    case Mode::Menu:             handleMenu();      break;
    case Mode::Motor1Control:    handleMotor1();    break;
    case Mode::Motor2Control:    handleMotor2();    break;
    case Mode::Motor3Control:    handleMotor3();    break;
    case Mode::AllMotorsControl: handleAllMotors(); break;
  }

  if (millis() - lastDisplayUpdateMs >= kDisplayIntervalMs) {
    switch (mode) {
      case Mode::Menu:
        oled.showMenu(menuIndex);
        break;
      case Mode::Motor1Control:
        oled.showMotor1Status(encoder.position(), sliderMotor1.positionMm(),
                              limitSwitch.engaged());
        break;
      case Mode::Motor2Control:
        oled.showMotor2Status(encoder.position(), panMotor2.positionDegrees());
        break;
      case Mode::Motor3Control:
        oled.showMotor3Status(encoder.position(), tiltMotor3.positionDegrees());
        break;
      case Mode::AllMotorsControl:
        oled.showAllMotorsStatus(sliderMotor1.positionMm(),
                                 panMotor2.positionDegrees(),
                                 tiltMotor3.positionDegrees(),
                                 limitSwitch.engaged());
        break;
    }
    lastDisplayUpdateMs = millis();
  }
}
