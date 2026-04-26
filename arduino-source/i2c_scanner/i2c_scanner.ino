// I2C bus scanner — diagnostic sketch.
// Open Serial Monitor at 9600 baud. Prints the address of every device
// that ACKs a probe write. Re-scans every 5 seconds.
//
// Expected for this project: SSD1306 OLED at 0x3C.
// If nothing is found, the OLED is unpowered, miswired, or damaged.
// If 0x3C is found but the screen is still blank, the controller is
// alive but the panel itself is likely dead.
//
// Wiring: SDA -> A4, SCL -> A5 on Arduino Uno/Nano.

#include <Wire.h>

void setup() {
  Wire.begin();
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println(F("I2C scanner ready."));
}

void loop() {
  Serial.println(F("Scanning..."));
  byte found = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();

    if (err == 0) {
      Serial.print(F("  device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    } else if (err == 4) {
      Serial.print(F("  unknown error at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
    }
  }

  if (found == 0) Serial.println(F("  no devices found"));
  else { Serial.print(F("  total: ")); Serial.println(found); }

  delay(5000);
}
