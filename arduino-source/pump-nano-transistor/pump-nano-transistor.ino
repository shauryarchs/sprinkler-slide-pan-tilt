// Pump prototype on a single Arduino Nano ESP32: the Nano drives the pump
// kit's relay through an NPN transistor, and the pump runs from its own
// 5 V supply (a wall charger, not the laptop). Controlled from the Serial
// Monitor. Run on a SPARE Nano ESP32 and breadboard, never the working
// motor/camera board. See PUMP_NANO_TRANSISTOR.md for the wiring.
//
// The relay module (SRD-05VDC-SL-C) is LOW-trigger and needs 5 V on IN to
// turn off, which a 3.3 V Nano pin can't give. The transistor (PN2222 /
// 2N2222A) switches IN to GND instead:
//   D2 HIGH -> transistor on  -> IN = GND      -> relay ON  -> pump runs
//   D2 LOW  -> transistor off -> IN = floating -> relay OFF -> pump stops
// While the Nano is booting or unplugged, D2 is not driven high, so the
// relay stays off. The relay's 5 V never reaches a Nano pin.
//
// Safety:
//   - Relay held off from start-up.
//   - Every run stops by itself when its timer ends; one command can ask
//     for at most kMaxRunS.
//
// Serial commands (115200 baud, line ending Newline, case-insensitive):
//   on           run for kDefaultRunS seconds
//   on <s>       run for <s> seconds (1 .. kMaxRunS)
//   cycle        kCycleCount times: kCycleOnS on, kCycleOffS off
//   loop         repeat kLoopOnS on, kLoopOffS off until stopped
//   off          stop now (also cancels a cycle or loop)
//   status       print state
//   help         list commands
//
// Button (optional) between A2 and GND: each press starts or stops
// "loop". It works even when the USB connection to the laptop has
// dropped. Nothing runs at power-up until a command or button press.
//
// Heartbeat: the built-in LED blinks once a second while the sketch runs
// (fast while the pump is on). If the Serial Monitor drops but the LED
// keeps blinking, only the USB connection dropped.
//
// Pins D2, A2 and the built-in LED (D13) are unused on the main board, so
// this wiring carries over.

namespace pins {
const int kPumpOut = D2;  // 1 kOhm -> transistor base: HIGH = pump on
const int kButton = A2;   // push button to GND (internal pull-up)
}  // namespace pins

const unsigned long kDefaultRunS = 3;
const unsigned long kMaxRunS = 30;
const unsigned long kCycleCount = 5;
const unsigned long kCycleOnS = 5;
const unsigned long kCycleOffS = 5;
const unsigned long kLoopOnS = 10;   // must be <= kMaxRunS
const unsigned long kLoopOffS = 5;
const unsigned long kButtonDebounceMs = 50;
const unsigned long kHeartbeatMs = 500;      // LED toggle period when idle
const unsigned long kHeartbeatFastMs = 100;  // ...while the pump is on

bool pumpOn = false;
unsigned long pumpStartMs = 0;
unsigned long pumpRunMs = 0;
unsigned long runCount = 0;

bool cycleActive = false;
unsigned long cycleStartMs = 0;
unsigned long cyclesStarted = 0;

bool loopActive = false;
unsigned long loopOffSinceMs = 0;
unsigned long loopRuns = 0;

bool buttonDown = false;
bool buttonRaw = false;
unsigned long buttonChangedMs = 0;

unsigned long lastBlinkMs = 0;
bool ledOn = false;

char cmdBuf[24];
size_t cmdLen = 0;

void setRelay(bool on) {
  digitalWrite(pins::kPumpOut, on ? HIGH : LOW);
}

void startPump(unsigned long seconds, const char* why) {
  pumpRunMs = seconds * 1000;
  pumpStartMs = millis();
  setRelay(true);
  if (pumpOn) return;
  pumpOn = true;
  runCount++;
  Serial.print(F("PUMP on  (run #"));
  Serial.print(runCount);
  Serial.print(F(", "));
  Serial.print(why);
  Serial.print(F(", auto-off in "));
  Serial.print(seconds);
  Serial.println(F("s)"));
}

void stopPump(const char* why) {
  setRelay(false);
  if (!pumpOn) return;
  pumpOn = false;
  Serial.print(F("PUMP off ("));
  Serial.print(why);
  Serial.print(F(", ran "));
  Serial.print((millis() - pumpStartMs) / 1000.0, 1);
  Serial.println(F("s)"));
}

void cancelCycle() {
  if (!cycleActive) return;
  cycleActive = false;
  Serial.print(F("CYCLE cancelled after "));
  Serial.print(cyclesStarted);
  Serial.print(F(" of "));
  Serial.println(kCycleCount);
}

void cancelLoop() {
  if (!loopActive) return;
  loopActive = false;
  Serial.print(F("LOOP stopped after "));
  Serial.print(loopRuns);
  Serial.println(F(" runs"));
}

void startLoop(const char* why) {
  cancelCycle();
  loopActive = true;
  loopRuns = 0;
  Serial.print(F("LOOP start ("));
  Serial.print(why);
  Serial.print(F("): "));
  Serial.print(kLoopOnS);
  Serial.print(F("s on, "));
  Serial.print(kLoopOffS);
  Serial.println(F("s off, until stopped"));
  loopRuns++;
  startPump(kLoopOnS, "loop");
}

// Restarts the pump kLoopOffS after each loop run ends.
void updateLoop() {
  if (!loopActive || pumpOn) return;
  if (millis() - loopOffSinceMs < kLoopOffS * 1000) return;
  loopRuns++;
  Serial.print(F("LOOP run "));
  Serial.println(loopRuns);
  startPump(kLoopOnS, "loop");
}

// Button press toggles loop on/off.
void readButton() {
  bool raw = digitalRead(pins::kButton) == LOW;
  unsigned long now = millis();
  if (raw != buttonRaw) {
    buttonRaw = raw;
    buttonChangedMs = now;
  }
  if (buttonRaw == buttonDown || now - buttonChangedMs < kButtonDebounceMs) return;
  buttonDown = buttonRaw;
  if (!buttonDown) return;  // act on press, not release
  if (loopActive || pumpOn) {
    cancelCycle();
    cancelLoop();
    stopPump("button");
  } else {
    startLoop("button");
  }
}

// Starts each "on" phase at a fixed time from the start of the cycle; the
// run timer in loop() ends it, leaving the rest of the period off.
void updateCycle() {
  if (!cycleActive || pumpOn) return;
  unsigned long period = (kCycleOnS + kCycleOffS) * 1000;
  if (millis() - cycleStartMs < cyclesStarted * period) return;
  if (cyclesStarted >= kCycleCount) {
    cycleActive = false;
    Serial.println(F("CYCLE done"));
    return;
  }
  cyclesStarted++;
  Serial.print(F("CYCLE "));
  Serial.print(cyclesStarted);
  Serial.print(F("/"));
  Serial.println(kCycleCount);
  startPump(kCycleOnS, "cycle");
}

void printStatus() {
  Serial.print(F("PUMP status: "));
  if (pumpOn) {
    Serial.print(F("on, "));
    Serial.print((millis() - pumpStartMs) / 1000.0, 1);
    Serial.print(F("s of "));
    Serial.print(pumpRunMs / 1000);
    Serial.print(F("s"));
  } else {
    Serial.print(F("off"));
  }
  if (loopActive) {
    Serial.print(F(", loop run "));
    Serial.print(loopRuns);
  }
  if (cycleActive) {
    Serial.print(F(", cycle "));
    Serial.print(cyclesStarted);
    Serial.print(F("/"));
    Serial.print(kCycleCount);
  }
  Serial.print(F(", runs="));
  Serial.println(runCount);
}

void printHelp() {
  Serial.print(F("Commands: on | on <1-"));
  Serial.print(kMaxRunS);
  Serial.println(F(" seconds> | cycle | loop | off | status | help"));
}

void handleCommand(char* cmd) {
  for (char* p = cmd; *p; p++) *p = tolower(*p);

  if (strcmp(cmd, "off") == 0) {
    cancelCycle();
    cancelLoop();
    if (pumpOn) {
      stopPump("command");
    } else {
      Serial.println(F("PUMP already off"));
    }
  } else if (strcmp(cmd, "on") == 0 || strncmp(cmd, "on ", 3) == 0) {
    long seconds = cmd[2] ? atol(cmd + 3) : (long)kDefaultRunS;
    if (seconds < 1 || seconds > (long)kMaxRunS) {
      Serial.print(F("PUMP refused: seconds must be 1-"));
      Serial.println(kMaxRunS);
      return;
    }
    cancelCycle();
    cancelLoop();
    if (pumpOn) {
      Serial.print(F("PUMP timer reset: auto-off in "));
      Serial.print(seconds);
      Serial.println(F("s"));
    }
    startPump((unsigned long)seconds, "command");
  } else if (strcmp(cmd, "loop") == 0) {
    if (pumpOn) stopPump("loop start");
    startLoop("command");
  } else if (strcmp(cmd, "cycle") == 0) {
    cancelLoop();
    if (pumpOn) stopPump("cycle start");
    cycleActive = true;
    cycleStartMs = millis();
    cyclesStarted = 0;
    Serial.print(F("CYCLE start: "));
    Serial.print(kCycleCount);
    Serial.print(F(" x ("));
    Serial.print(kCycleOnS);
    Serial.print(F("s on, "));
    Serial.print(kCycleOffS);
    Serial.println(F("s off)"));
    updateCycle();
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
  // Relay off before anything else: D2 LOW = transistor off = relay off.
  // Written before and after pinMode so the pin never starts high.
  digitalWrite(pins::kPumpOut, LOW);
  pinMode(pins::kPumpOut, OUTPUT);
  digitalWrite(pins::kPumpOut, LOW);
  pinMode(pins::kButton, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  delay(1500);  // give the USB serial port time to enumerate
  Serial.println(F("# EmberSensor pump prototype (Nano ESP32 + transistor)"));
  Serial.println(F("# pump is OFF. Put the pump in water before running it."));
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
  readButton();
  heartbeat();

  if (pumpOn && millis() - pumpStartMs >= pumpRunMs) {
    stopPump(cycleActive ? "cycle" : loopActive ? "loop" : "timer");
    loopOffSinceMs = millis();
  }
  updateCycle();
  updateLoop();

  // Re-assert the relay every pass so the pin always matches pumpOn.
  setRelay(pumpOn);
}
