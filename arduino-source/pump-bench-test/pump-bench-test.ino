// Bench test for the water pump on a SPARE Arduino Nano ESP32 and a
// SPARE breadboard -- never the working motor/camera board. Turns the
// pump on and off from the Serial Monitor so the relay wiring and the
// control logic can be checked safely. See PUMP_BENCH_TEST.md for the
// wiring and the step-by-step test.
//
// The relay module (SRD-05VDC-SL-C) is LOW-trigger and needs 5 V to
// turn off, which a 3.3 V Nano ESP32 pin cannot give. So D2 drives an
// NPN transistor that pulls the relay's IN to GND:
//   D2 HIGH -> transistor on  -> IN = GND      -> relay ON  (pump runs)
//   D2 LOW  -> transistor off -> IN = floating -> relay OFF (pump stops)
// While the board is booting or unplugged, D2 is not driven high, so
// the pump stays off.
//
// Serial commands (newline-terminated, case-insensitive):
//   on           run the pump for kDefaultRunS seconds, then stop
//   on <s>       run for <s> seconds (1 .. kMaxRunS)
//   off          stop now
//   status       print pump state
//   help         list commands
// Every run stops by itself when its timer ends.

#include <esp_system.h>

namespace pins {
const int kPump = D2;  // unused on the main board, so the wiring carries over
}  // namespace pins

const unsigned long kDefaultRunS = 3;
const unsigned long kMaxRunS = 30;  // never run longer than this per command

bool pumpOn = false;
unsigned long pumpStartMs = 0;
unsigned long pumpRunMs = 0;
unsigned long runCount = 0;

char cmdBuf[24];
size_t cmdLen = 0;

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply voltage dipped -- likely the pump surge)";
    case ESP_RST_EXT:      return "reset button";
    default:               return "other";
  }
}

void setPump(bool on, const char* why) {
  digitalWrite(pins::kPump, on ? HIGH : LOW);
  if (on == pumpOn) return;
  pumpOn = on;
  if (on) {
    pumpStartMs = millis();
    runCount++;
    Serial.printf("PUMP on  (run #%lu, auto-off in %lus)\n", runCount, pumpRunMs / 1000);
  } else {
    Serial.printf("PUMP off (%s, ran %.1fs)\n", why, (millis() - pumpStartMs) / 1000.0f);
  }
}

void printStatus() {
  if (pumpOn) {
    unsigned long elapsed = millis() - pumpStartMs;
    Serial.printf("PUMP status: on, %.1fs of %lus, runs=%lu\n", elapsed / 1000.0f,
                  pumpRunMs / 1000, runCount);
  } else {
    Serial.printf("PUMP status: off, runs=%lu\n", runCount);
  }
}

void printHelp() {
  Serial.printf("Commands: on | on <1-%lu seconds> | off | status | help\n", kMaxRunS);
}

void handleCommand(char* cmd) {
  for (char* p = cmd; *p; p++) *p = tolower(*p);

  if (strcmp(cmd, "off") == 0) {
    if (pumpOn) {
      setPump(false, "command");
    } else {
      Serial.println("PUMP already off");
    }
  } else if (strcmp(cmd, "on") == 0 || strncmp(cmd, "on ", 3) == 0) {
    long seconds = cmd[2] ? atol(cmd + 3) : (long)kDefaultRunS;
    if (seconds < 1 || seconds > (long)kMaxRunS) {
      Serial.printf("PUMP refused: seconds must be 1-%lu\n", kMaxRunS);
      return;
    }
    pumpRunMs = (unsigned long)seconds * 1000;
    if (pumpOn) {
      pumpStartMs = millis();  // restart the timer with the new length
      Serial.printf("PUMP timer reset: auto-off in %lds\n", seconds);
    } else {
      setPump(true, "command");
    }
  } else if (strcmp(cmd, "status") == 0) {
    printStatus();
  } else {
    printHelp();
  }
}

void readCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        cmdLen = 0;
        handleCommand(cmdBuf);
      }
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

void setup() {
  // Pump off before anything else: set the level, then make it an output.
  digitalWrite(pins::kPump, LOW);
  pinMode(pins::kPump, OUTPUT);

  Serial.begin(115200);
  delay(1500);  // give the USB serial port time to enumerate
  Serial.println("# EmberSensor pump bench test (spare board only)");
  Serial.printf("# reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.println("# pump is OFF. Put the pump in water before typing 'on'.");
  printHelp();
}

void loop() {
  readCommands();

  if (pumpOn && millis() - pumpStartMs >= pumpRunMs) {
    setPump(false, "timer");
  }
}
