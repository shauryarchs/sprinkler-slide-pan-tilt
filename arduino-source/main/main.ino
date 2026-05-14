#include <Wire.h>

#include "Display.h"
#include "Encoder.h"
#include "LimitSwitch.h"
#include "Stepper.h"

// Pin assignments use Arduino Nano ESP32 silkscreen labels (D3/D4/D5/D6/D7/D10).
// The core resolves them to the right ESP32-S3 GPIOs.
namespace pins {
const int kEncoderSw = D3;
const int kEncoderDt = D4;
const int kEncoderClk = D5;
const int kStepperDir = D6;
const int kStepperStep = D7;
const int kLimit = D10;
}  // namespace pins

// display.display() blocks ~30 ms on I2C; refreshing every 250 ms keeps
// the UI readable while limiting stepping duty loss to ~12%.
const unsigned long kDisplayIntervalMs = 250;

Encoder encoder(pins::kEncoderSw, pins::kEncoderDt, pins::kEncoderClk);
LimitSwitch limitSwitch(pins::kLimit);
Stepper stepper(pins::kStepperDir, pins::kStepperStep);
Display oled;

unsigned long lastDisplayUpdateMs = 0;

void rehome() {
  // Drop the encoder ISR for the duration of homing so dial spins are
  // ignored; resync the SW state after so a still-held button doesn't
  // immediately trigger another long-press.
  encoder.suspend();
  oled.showHomingMessage();
  stepper.home(limitSwitch);
  encoder.reset();
  encoder.resume();
  encoder.syncSwState();
  lastDisplayUpdateMs = 0;  // force a refresh on the next loop tick
}

void setup() {
  limitSwitch.begin();
  stepper.begin();

  Wire.begin();
  Wire.setClock(400000);

  // If the OLED is missing show*() calls are silent no-ops, so the motor
  // still runs without it.
  oled.begin();

  oled.showHomingMessage();
  stepper.home(limitSwitch);

  // Attach the encoder ISR after homing so dial spins during the homing
  // phase are ignored. syncSwState() suppresses a spurious long-press if
  // the user is holding SW through boot.
  encoder.begin();
  encoder.syncSwState();
}

void loop() {
  encoder.update();

  if (encoder.consumeLongPress()) {
    rehome();
  }

  int dial = encoder.position();
  stepper.update(dial, limitSwitch);

  if (millis() - lastDisplayUpdateMs >= kDisplayIntervalMs) {
    oled.showStatus(dial, stepper.positionMm(),
                    stepper.currentSpeedTenthsMmPerSec(),
                    limitSwitch.engaged());
    lastDisplayUpdateMs = millis();
  }
}
