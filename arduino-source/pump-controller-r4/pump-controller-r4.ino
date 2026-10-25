// Pump bench test on a single Arduino UNO R4 WiFi: the R4 switches the
// pump kit's relay, and the pump runs from its own 5 V supply (a wall
// charger, not the laptop). Controlled from the Serial Monitor. See
// PUMP_BENCH_TEST.md for the wiring and the step-by-step test.
//
// The relay module (SRD-05VDC-SL-C) is LOW-trigger and needs 5 V on IN to
// turn off, so it is driven from a 5 V board (the R4; an UNO R3 also
// works):
//   pin 8 LOW  -> relay ON  -> COM connects to NO -> pump runs
//   pin 8 HIGH -> relay OFF -> pump stops
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
// Button (optional) between pin 2 and GND: each press starts or stops
// "loop". It works even when the USB connection to the laptop has
// dropped. Nothing runs at power-up until a command or button press.

namespace pins {
const int kRelayIn = 8;  // to relay IN: LOW = relay on, HIGH = relay off
const int kButton = 2;   // push button to GND (internal pull-up)
}  // namespace pins

const unsigned long kDefaultRunS = 3;
const unsigned long kMaxRunS = 30;
const unsigned long kCycleCount = 5;
const unsigned long kCycleOnS = 5;
const unsigned long kCycleOffS = 5;
const unsigned long kLoopOnS = 30;   // must be <= kMaxRunS
const unsigned long kLoopOffS = 5;
const unsigned long kButtonDebounceMs = 50;

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

char cmdBuf[24];
size_t cmdLen = 0;

void setRelay(bool on) {
  digitalWrite(pins::kRelayIn, on ? LOW : HIGH);
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
  // Relay off before anything else: HIGH on IN = off for this module.
  // Written before AND after pinMode: on the UNO R3 the first write sets
  // the level the pin starts with; on the UNO R4 it may not stick, so the
  // pin would start LOW (relay on) without the second write.
  digitalWrite(pins::kRelayIn, HIGH);
  pinMode(pins::kRelayIn, OUTPUT);
  digitalWrite(pins::kRelayIn, HIGH);
  pinMode(pins::kButton, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println(F("# EmberSensor pump controller (single board)"));
  Serial.println(F("# pump is OFF. Put the pump in water before running it."));
  printHelp();
}

void loop() {
  readCommands();
  readButton();

  if (pumpOn && millis() - pumpStartMs >= pumpRunMs) {
    stopPump(cycleActive ? "cycle" : loopActive ? "loop" : "timer");
    loopOffSinceMs = millis();
  }
  updateCycle();
  updateLoop();

  // Re-assert the relay every pass so the pin always matches pumpOn.
  setRelay(pumpOn);
}
