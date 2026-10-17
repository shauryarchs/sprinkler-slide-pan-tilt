// Pump controller for an Arduino UNO R3. The UNO switches the pump kit's
// relay; the Nano ESP32 only sends a one-way "spray" signal. See
// PUMP_BENCH_TEST.md for the wiring and the step-by-step test.
//
// Why two boards: the relay module (SRD-05VDC-SL-C) is LOW-trigger and
// needs 5 V on IN to turn off. The UNO's 5 V pins can do that directly;
// the Nano ESP32's 3.3 V pins cannot. The pump's power and start-up
// surge also stay on the UNO, away from the board that aims.
//
// Signal from the Nano (pin 7, with a resistor from pin 7 to GND):
//   HIGH (Nano D2 = 3.3 V) -> spray
//   LOW / wire loose / Nano off or booting -> pump off (the resistor
//   holds the pin low)
//
// Safety: the pump never runs longer than kMaxRunS in one go. After that
// it stays off until the signal goes LOW again, so a stuck signal can't
// run the pump forever.
//
// Serial commands for testing without the Nano (115200 baud, Newline):
//   on           run for kDefaultRunS seconds
//   on <s>       run for <s> seconds (1 .. kMaxRunS)
//   off          stop now (also ignores a HIGH signal until it goes LOW)
//   status       print state
//   help         list commands

namespace pins {
const int kSignalIn = 7;  // from Nano D2, with a resistor to GND
const int kRelayIn = 8;   // to relay IN: LOW = relay on, HIGH = relay off
}  // namespace pins

const unsigned long kDefaultRunS = 3;
const unsigned long kMaxRunS = 30;
const unsigned long kDebounceMs = 50;  // signal must be steady this long

bool pumpOn = false;
unsigned long pumpStartMs = 0;
unsigned long runCount = 0;

// Manual (Serial) run.
bool manualRun = false;
unsigned long manualStartMs = 0;
unsigned long manualRunMs = 0;

// Nano signal, debounced.
bool signalHigh = false;
bool rawSignal = false;
unsigned long rawChangedMs = 0;
// Set after a max-runtime stop or an "off" command while the signal is
// HIGH; cleared when the signal goes LOW.
bool signalLockedOut = false;

char cmdBuf[24];
size_t cmdLen = 0;

void setRelay(bool on) {
  digitalWrite(pins::kRelayIn, on ? LOW : HIGH);
}

void startPump(const char* why) {
  setRelay(true);
  if (pumpOn) return;
  pumpOn = true;
  pumpStartMs = millis();
  runCount++;
  Serial.print(F("PUMP on  (run #"));
  Serial.print(runCount);
  Serial.print(F(", "));
  Serial.print(why);
  Serial.println(F(")"));
}

void stopPump(const char* why) {
  setRelay(false);
  manualRun = false;
  if (!pumpOn) return;
  pumpOn = false;
  Serial.print(F("PUMP off ("));
  Serial.print(why);
  Serial.print(F(", ran "));
  Serial.print((millis() - pumpStartMs) / 1000.0, 1);
  Serial.println(F("s)"));
}

void printStatus() {
  Serial.print(F("PUMP status: "));
  Serial.print(pumpOn ? F("on") : F("off"));
  if (pumpOn) {
    Serial.print(F(" for "));
    Serial.print((millis() - pumpStartMs) / 1000.0, 1);
    Serial.print(F("s"));
  }
  Serial.print(F(", signal="));
  Serial.print(signalHigh ? F("HIGH") : F("LOW"));
  if (signalLockedOut) Serial.print(F(" (ignored until LOW)"));
  Serial.print(F(", manual="));
  Serial.print(manualRun ? F("yes") : F("no"));
  Serial.print(F(", runs="));
  Serial.println(runCount);
}

void printHelp() {
  Serial.print(F("Commands: on | on <1-"));
  Serial.print(kMaxRunS);
  Serial.println(F(" seconds> | off | status | help"));
}

void handleCommand(char* cmd) {
  for (char* p = cmd; *p; p++) *p = tolower(*p);

  if (strcmp(cmd, "off") == 0) {
    if (signalHigh) signalLockedOut = true;
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
    manualRun = true;
    manualStartMs = millis();
    manualRunMs = (unsigned long)seconds * 1000;
    if (pumpOn) {
      // Still capped by kMaxRunS from when the pump started.
      Serial.print(F("PUMP manual timer set: "));
      Serial.print(seconds);
      Serial.println(F("s"));
    } else {
      startPump("command");
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

void readSignal() {
  bool raw = digitalRead(pins::kSignalIn) == HIGH;
  unsigned long now = millis();
  if (raw != rawSignal) {
    rawSignal = raw;
    rawChangedMs = now;
  }
  if (rawSignal != signalHigh && now - rawChangedMs >= kDebounceMs) {
    signalHigh = rawSignal;
    Serial.print(F("SIGNAL "));
    Serial.println(signalHigh ? F("HIGH (spray)") : F("LOW (stop)"));
    if (!signalHigh) signalLockedOut = false;
  }
}

void setup() {
  // Relay off before anything else: HIGH on IN = off for this module.
  digitalWrite(pins::kRelayIn, HIGH);
  pinMode(pins::kRelayIn, OUTPUT);
  pinMode(pins::kSignalIn, INPUT);  // external resistor to GND, no pull-up

  Serial.begin(115200);
  Serial.println(F("# EmberSensor pump controller (UNO R3)"));
  Serial.println(F("# pump is OFF. Put the pump in water before running it."));
  printHelp();
}

void loop() {
  readCommands();
  readSignal();

  unsigned long now = millis();
  bool wantSignal = signalHigh && !signalLockedOut;

  if (manualRun && now - manualStartMs >= manualRunMs) {
    manualRun = false;
    if (pumpOn && !wantSignal) {
      stopPump("timer");
      return;
    }
  }

  if (pumpOn && now - pumpStartMs >= kMaxRunS * 1000UL) {
    if (signalHigh) signalLockedOut = true;
    stopPump("max runtime");
  } else if (wantSignal || manualRun) {
    startPump(wantSignal ? "signal" : "command");
  } else if (pumpOn) {
    stopPump("signal");
  }
}
