// Nano ESP32 side of the pump bench test: sends the one-way "spray"
// signal to the UNO R3 that switches the pump relay
// (arduino-source/pump-controller-uno). Run on a SPARE Nano ESP32 and a
// SPARE breadboard -- never the working motor/camera board. See
// PUMP_BENCH_TEST.md for the wiring and the step-by-step test.
//
//   D2 HIGH (3.3 V) -> UNO pin 7 HIGH -> pump on
//   D2 LOW          -> UNO pin 7 LOW  -> pump off
// A resistor from UNO pin 7 to GND keeps the pump off while this board
// is booting or unplugged. The UNO never drives this wire, so 5 V never
// reaches the Nano.
//
// Serial commands (newline-terminated, case-insensitive):
//   on           signal "spray" for kDefaultRunS seconds, then stop
//   on <s>       signal for <s> seconds (1 .. kMaxRunS)
//   off          stop now
//   status       print signal state
//   help         list commands
// Every run stops by itself when its timer ends. The UNO has its own
// maximum runtime as well.
//
// Heartbeat: the built-in LED blinks once a second while the sketch is
// running (fast while signalling "spray"). If the Serial Monitor stops
// responding but the LED keeps blinking, only the USB connection dropped.

#include <esp_system.h>

namespace pins {
const int kPump = D2;  // to UNO pin 7; D2 is unused on the main board
}  // namespace pins

const unsigned long kDefaultRunS = 3;
const unsigned long kMaxRunS = 30;  // never run longer than this per command
const unsigned long kHeartbeatMs = 500;      // LED toggle period when idle
const unsigned long kHeartbeatFastMs = 100;  // ...while signalling "spray"

bool pumpOn = false;
unsigned long pumpStartMs = 0;
unsigned long pumpRunMs = 0;
unsigned long runCount = 0;

unsigned long lastBlinkMs = 0;
bool ledOn = false;

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
    case ESP_RST_BROWNOUT: return "brownout (supply voltage dipped)";
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
    Serial.printf("SIGNAL on  (run #%lu, auto-off in %lus)\n", runCount, pumpRunMs / 1000);
  } else {
    Serial.printf("SIGNAL off (%s, ran %.1fs)\n", why, (millis() - pumpStartMs) / 1000.0f);
  }
}

void printStatus() {
  if (pumpOn) {
    unsigned long elapsed = millis() - pumpStartMs;
    Serial.printf("SIGNAL status: on, %.1fs of %lus, runs=%lu\n", elapsed / 1000.0f,
                  pumpRunMs / 1000, runCount);
  } else {
    Serial.printf("SIGNAL status: off, runs=%lu\n", runCount);
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
      Serial.println("SIGNAL already off");
    }
  } else if (strcmp(cmd, "on") == 0 || strncmp(cmd, "on ", 3) == 0) {
    long seconds = cmd[2] ? atol(cmd + 3) : (long)kDefaultRunS;
    if (seconds < 1 || seconds > (long)kMaxRunS) {
      Serial.printf("SIGNAL refused: seconds must be 1-%lu\n", kMaxRunS);
      return;
    }
    pumpRunMs = (unsigned long)seconds * 1000;
    if (pumpOn) {
      pumpStartMs = millis();  // restart the timer with the new length
      Serial.printf("SIGNAL timer reset: auto-off in %lds\n", seconds);
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
  // Signal off before anything else: set the level, then make it an output.
  digitalWrite(pins::kPump, LOW);
  pinMode(pins::kPump, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  delay(1500);  // give the USB serial port time to enumerate
  Serial.println("# EmberSensor pump signal test (spare Nano -> UNO pin 7)");
  Serial.printf("# reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.println("# signal is OFF. Put the pump in water before typing 'on'.");
  printHelp();
}

void heartbeat() {
  unsigned long now = millis();
  if (now - lastBlinkMs < (pumpOn ? kHeartbeatFastMs : kHeartbeatMs)) return;
  lastBlinkMs = now;
  ledOn = !ledOn;
  digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
}

void loop() {
  readCommands();
  heartbeat();

  if (pumpOn && millis() - pumpStartMs >= pumpRunMs) {
    setPump(false, "timer");
  }
}
