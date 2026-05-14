#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pins use Arduino Nano ESP32 silkscreen labels (D3/D4/D5/D6/D7/D10). The
// core resolves them to the right ESP32-S3 GPIOs.
const int ENCODER_SW = D3;
const int ENCODER_DT = D4;
const int ENCODER_CLK = D5;
const int DIR_PIN = D6;
const int STEP_PIN = D7;
const int LIMIT_PIN = D10;

// 200 full-steps/rev * 32 microsteps (MS1=VDD, MS2=GND) = 6400 steps/rev.
// 160 steps/mm assumes GT2 belt + 20T pulley (40 mm/rev).
const long STEPS_PER_MM = 160;
const long MAX_POSITION_MM = 1000;
const long MIN_POSITION_MM = 5;
const long MAX_POSITION_STEPS = MAX_POSITION_MM * STEPS_PER_MM;
const long MIN_POSITION_STEPS = MIN_POSITION_MM * STEPS_PER_MM;

// A4988 needs >=1 µs STEP HIGH; 3 µs leaves margin without slowing the max rate.
const unsigned int STEP_PULSE_WIDTH_US = 3;

// Two-pass homing: fast approach for speed, slow re-engage for a repeatable
// trip point, with a back-off and clearance margin after each pass.
const unsigned long HOMING_STEP_INTERVAL_FAST_US = 200;  // ~30 mm/s
const unsigned long HOMING_STEP_INTERVAL_SLOW_US = 800;  // ~7.5 mm/s
// Steps to back off after the limit switch engages, so the carriage isn't
// resting on a depressed lever. 160 microsteps ~= 1 mm of clearance — wide
// enough to ride out switch hysteresis and small mechanical play.
const int HOMING_BACKOFF_STEPS = 160;

// Encoder dial drives a signed speed setpoint in [-ENCODER_MAX, +ENCODER_MAX].
// Sign picks direction (positive = CW, negative = CCW), magnitude scales the
// step interval linearly: |dial|=1 -> slowest, |dial|=ENCODER_MAX -> fastest.
// At dial = 0 the motor is stopped.
const int ENCODER_MAX = 20;
const unsigned long STEP_INTERVAL_MIN_US = 120;
const unsigned long STEP_INTERVAL_MAX_US = 3000;
const unsigned long ENCODER_DEBOUNCE_US = 1500;

// Hold SW for this long to trigger a full re-home. Shorter presses are an
// immediate stop only.
const unsigned long LONG_PRESS_MS = 1500;

// A CCW limit trip within this many steps of the soft floor is treated as
// accumulated drift and auto-re-zeros the counter. Trips outside the window
// require an explicit long-press re-home — silent re-zero from far away
// would mask real bugs.
const long DRIFT_RECOVERY_WINDOW_STEPS = 1600;  // ~10 mm

static_assert(STEP_INTERVAL_MIN_US < STEP_INTERVAL_MAX_US,
              "min step interval must be smaller than max (fast < slow)");
static_assert(MIN_POSITION_STEPS < MAX_POSITION_STEPS,
              "soft floor must be below the far limit");
static_assert(ENCODER_MAX > 0, "encoder range must be positive");
static_assert(HOMING_BACKOFF_STEPS > 0, "back-off must clear the switch");

const int DIR_CW = LOW;
const int DIR_CCW = HIGH;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// False if display.begin() failed at startup. All display.* calls are then
// skipped so the motor still runs without the OLED, and LED_BUILTIN blinks
// at 1 Hz to flag the fallback during bring-up.
bool displayOk = false;
unsigned long lastLedBlink = 0;
bool ledBlinkState = false;

// KY-040 SW state machine. Short press = stop, long press = re-home.
int encSwState = HIGH;
int lastEncSwReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long pressStartMs = 0;
bool longPressFired = false;

unsigned long lastDisplayUpdate = 0;
// display.display() blocks ~30 ms on the I2C frame transfer; at 100 ms that
// was eating ~30% of stepping time and the motor jittered audibly at high
// dial values. 250 ms keeps the UI readable while cutting duty loss to ~12%.
const unsigned long displayInterval = 250;

long stepperPosition = 0;
uint8_t stepperDir = DIR_CW;
unsigned long lastStepUs = 0;

// KY-040 quadrature state. CLK and DT idle HIGH (onboard 10k pull-ups).
// Interrupt-driven: ENCODER_CLK falling edge fires onEncoderClk(), which
// reads DT to decide direction (DT HIGH -> CW dial++, DT LOW -> CCW dial--).
// Running off an ISR matters because display.display() blocks the main
// loop for ~30 ms during the I2C frame transfer — polling there would
// miss ticks when the dial is spun quickly. ENCODER_DEBOUNCE_US suppresses
// switch-bounce double-counts on the same physical detent.
volatile int encoderPos = 0;
volatile unsigned long lastEncoderIsrUs = 0;

void IRAM_ATTR onEncoderClk() {
  unsigned long now = micros();
  if (now - lastEncoderIsrUs < ENCODER_DEBOUNCE_US) return;
  lastEncoderIsrUs = now;
  int dt = digitalRead(ENCODER_DT);
  if (dt == HIGH) {
    if (encoderPos < ENCODER_MAX) encoderPos++;
  } else {
    if (encoderPos > -ENCODER_MAX) encoderPos--;
  }
}

void stepPulse() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(STEP_PULSE_WIDTH_US);
  digitalWrite(STEP_PIN, LOW);
}

void stepHomingPulse(unsigned long intervalUs) {
  stepPulse();
  delayMicroseconds(intervalUs);
}

void homeStepper() {
  // Pass 1: fast CCW until the switch trips. If already engaged at boot
  // we skip this and drop into the back-off.
  digitalWrite(DIR_PIN, DIR_CCW);
  stepperDir = DIR_CCW;
  delayMicroseconds(5);
  while (digitalRead(LIMIT_PIN) == HIGH) {
    stepHomingPulse(HOMING_STEP_INTERVAL_FAST_US);
  }

  // Back off CW until the switch releases, then clearance steps.
  digitalWrite(DIR_PIN, DIR_CW);
  stepperDir = DIR_CW;
  delayMicroseconds(5);
  while (digitalRead(LIMIT_PIN) == LOW) {
    stepHomingPulse(HOMING_STEP_INTERVAL_FAST_US);
  }
  for (int i = 0; i < HOMING_BACKOFF_STEPS; i++) {
    stepHomingPulse(HOMING_STEP_INTERVAL_FAST_US);
  }

  // Pass 2: slow CCW re-engage for an accurate, repeatable trip point.
  digitalWrite(DIR_PIN, DIR_CCW);
  stepperDir = DIR_CCW;
  delayMicroseconds(5);
  while (digitalRead(LIMIT_PIN) == HIGH) {
    stepHomingPulse(HOMING_STEP_INTERVAL_SLOW_US);
  }

  // Final back-off so we end with the switch released and clearance set.
  digitalWrite(DIR_PIN, DIR_CW);
  stepperDir = DIR_CW;
  delayMicroseconds(5);
  while (digitalRead(LIMIT_PIN) == LOW) {
    stepHomingPulse(HOMING_STEP_INTERVAL_SLOW_US);
  }
  for (int i = 0; i < HOMING_BACKOFF_STEPS; i++) {
    stepHomingPulse(HOMING_STEP_INTERVAL_SLOW_US);
  }

  stepperPosition = 0;
}

void showHomingMessage() {
  if (!displayOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(F("Homing..."));
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.println(F("Please wait,"));
  display.setCursor(0, 34);
  display.println(F("don't press any"));
  display.setCursor(0, 46);
  display.println(F("buttons or dials"));
  display.display();
}

void triggerRehome() {
  // Drop the encoder ISR during homing so dial spins are ignored.
  detachInterrupt(digitalPinToInterrupt(ENCODER_CLK));
  showHomingMessage();
  homeStepper();
  noInterrupts();
  encoderPos = 0;
  interrupts();
  lastStepUs = micros();
  lastDisplayUpdate = 0;  // force an immediate refresh after re-home
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), onEncoderClk, FALLING);
}

void updateDisplay(int dial, long posMm, unsigned long spdTenths) {
  if (!displayOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(F("Ember Sensor"));

  display.setCursor(0, 10);
  display.println(F("www.embersensor.com"));

  display.setCursor(0, 22);
  display.print(F("Dial: "));
  if (dial > 0) display.print('+');
  display.print(dial);
  display.print(' ');
  if (dial > 0) display.print(F("CW"));
  else if (dial < 0) display.print(F("CCW"));
  else display.print(F("STOP"));

  // Compact labels to fit "P:1000mm S:52.0mm/s" in the 128 px width.
  display.setCursor(0, 33);
  display.print(F("P:"));
  display.print(posMm);
  display.print(F("mm S:"));
  display.print(spdTenths / 10);
  display.print('.');
  display.print(spdTenths % 10);
  display.print(F("mm/s"));

  display.setCursor(0, 44);
  display.print(F("Limit: "));
  display.print(digitalRead(LIMIT_PIN) == LOW ? F("LOW (hit)") : F("HIGH"));

  display.setCursor(0, 56);
  display.print(F("By, Shaurya Varshnay"));

  display.display();
}

void setup() {
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(LED_BUILTIN, LOW);

  Wire.begin();
  Wire.setClock(400000);

  // OLED bring-up: if begin() reports failure, run motor-only with a
  // 1 Hz LED heartbeat instead of crashing or silently no-op'ing.
  displayOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (displayOk) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
  }

  showHomingMessage();
  homeStepper();
  lastStepUs = micros();

  // Sync the SW state machine to the actual pin so a user holding SW
  // through boot doesn't get registered as a falling edge on the first
  // loop iteration (which would trip a spurious long-press re-home
  // LONG_PRESS_MS later).
  encSwState = digitalRead(ENCODER_SW);
  lastEncSwReading = encSwState;
  lastDebounceTime = millis();

  // Attach the encoder ISR after homing so dial spins during the homing
  // phase are ignored (encoderPos starts at 0 when the user can drive).
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), onEncoderClk, FALLING);
}

void loop() {
  int reading = digitalRead(ENCODER_SW);
  if (reading != lastEncSwReading) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != encSwState) {
      encSwState = reading;
      if (encSwState == LOW) {
        // Falling edge: immediate stop. The long-press check below fires
        // a full re-home if the user keeps holding past LONG_PRESS_MS.
        noInterrupts();
        encoderPos = 0;
        interrupts();
        pressStartMs = millis();
        longPressFired = false;
      }
    }
    if (encSwState == LOW && !longPressFired &&
        (millis() - pressStartMs) > LONG_PRESS_MS) {
      longPressFired = true;
      triggerRehome();
    }
  }
  lastEncSwReading = reading;

  // Heartbeat blink when running OLED-less so the fallback is visible
  // during bring-up.
  if (!displayOk && (millis() - lastLedBlink) >= 500) {
    ledBlinkState = !ledBlinkState;
    digitalWrite(LED_BUILTIN, ledBlinkState ? HIGH : LOW);
    lastLedBlink = millis();
  }

  // Snapshot encoderPos once per iteration — the ISR can change it at any
  // time, so re-reading would risk inconsistent direction/magnitude.
  int dial = encoderPos;
  bool wantMove = (dial != 0);
  uint8_t desiredDir = (dial > 0) ? DIR_CW : DIR_CCW;
  int mag = (dial > 0) ? dial : -dial;
  unsigned long targetInterval = STEP_INTERVAL_MAX_US;
  if (mag > 0) {
    targetInterval = map(mag, 1, ENCODER_MAX,
                         STEP_INTERVAL_MAX_US, STEP_INTERVAL_MIN_US);
  }

  // Edge guards.
  if (desiredDir == DIR_CW && stepperPosition >= MAX_POSITION_STEPS) {
    wantMove = false;
  }
  if (desiredDir == DIR_CCW) {
    if (digitalRead(LIMIT_PIN) == LOW) {
      wantMove = false;
      if (stepperPosition < MIN_POSITION_STEPS + DRIFT_RECOVERY_WINDOW_STEPS) {
        stepperPosition = 0;
      }
    } else if (stepperPosition <= MIN_POSITION_STEPS) {
      wantMove = false;
    }
  }

  if (wantMove) {
    if (desiredDir != stepperDir) {
      stepperDir = desiredDir;
      digitalWrite(DIR_PIN, desiredDir);
      delayMicroseconds(5);
      lastStepUs = micros();
    }
    unsigned long nowUs = micros();
    if (nowUs - lastStepUs >= targetInterval) {
      stepPulse();
      if (stepperDir == DIR_CW) stepperPosition++;
      else stepperPosition--;
      lastStepUs = nowUs;
    }
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    // Speed in tenths of mm/s so the slow end (~2 mm/s) shows meaningful
    // resolution. Zero when edge-stopped or dial at zero.
    unsigned long spdTenths = 0;
    if (wantMove) {
      spdTenths = (10UL * 1000000UL) /
                  (targetInterval * (unsigned long)STEPS_PER_MM);
    }
    // Round to nearest mm so the display matches what the user perceives
    // (e.g. 159 steps = 0.99 mm should read as 1, not 0).
    long posMm = (stepperPosition + STEPS_PER_MM / 2) / STEPS_PER_MM;
    updateDisplay(dial, posMm, spdTenths);
    lastDisplayUpdate = millis();
  }
}
