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
  void showStatus(int dial, long posMm, unsigned long spdTenths,
                  bool limitEngaged);

 private:
  Adafruit_SSD1306 display_;
  uint8_t addr_;
  bool ok_;
};
