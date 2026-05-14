#pragma once

#include <Arduino.h>

// Non-blocking 1 Hz heartbeat for the onboard LED. Used to signal
// degraded modes (e.g. OLED missing) without a serial monitor.
class StatusLed {
 public:
  explicit StatusLed(int pin, unsigned long blinkIntervalMs = 500);
  void begin();
  void setBlinking(bool blinking);
  void update();

 private:
  int pin_;
  unsigned long blinkIntervalMs_;
  bool blinking_;
  bool ledState_;
  unsigned long lastBlinkMs_;
};
