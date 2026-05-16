#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// SSD1306 wrapper that tolerates a missing OLED — begin() reports the
// failure, and subsequent show*() calls become no-ops so the motor still
// runs. The screen layout (and the "Ember Sensor" header) lives here.
class Display {
 public:
  explicit Display(uint8_t addr = 0x3C);

  // Returns false if the OLED didn't respond on I2C.
  bool begin();
  bool isOk() const { return ok_; }

  void showHomingMessage();
  // Motor-selection screen shown while no motor is active. selection
  // indexes the menu items (0 = Motor 1, 1 = Motor 2, 2 = Motor 3,
  // 3 = All Motors).
  void showMenu(int selection);
  // Status screens for each motor while it's being controlled.
  void showMotor1Status(int dial, long posMm, bool limitEngaged);
  void showMotor2Status(int dial, long posDeg);
  void showMotor3Status(int dial, long posDeg);
  // Status screen for the "all motors" demo mode.
  void showAllMotorsStatus(long sliderMm, long panDeg, long tiltDeg,
                           bool limitEngaged);

 private:
  Adafruit_SSD1306 display_;
  uint8_t addr_;
  bool ok_;
};
