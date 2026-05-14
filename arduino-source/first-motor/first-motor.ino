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

const unsigned long HOMING_STEP_INTERVAL_US = 200;

// Encoder dial drives a signed speed setpoint in [-ENCODER_MAX, +ENCODER_MAX].
// Sign picks direction (positive = CW, negative = CCW), magnitude scales the
// step interval linearly: |dial|=1 -> slowest, |dial|=ENCODER_MAX -> fastest.
// At dial = 0 the motor is stopped.
const int ENCODER_MAX = 20;
const unsigned long STEP_INTERVAL_MIN_US = 120;
const unsigned long STEP_INTERVAL_MAX_US = 3000;
const unsigned long ENCODER_DEBOUNCE_US = 1500;

const int DIR_CW = LOW;
const int DIR_CCW = HIGH;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// KY-040 SW press resets the dial to 0 — acts as an immediate stop.
int encSwState = HIGH;
int lastEncSwReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 100;

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

void stepHomingPulse() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(HOMING_STEP_INTERVAL_US);
}

void homeStepper() {
  // Drive CCW until the limit switch engages. If it's already engaged at
  // boot, the loop body is skipped and we just zero the position.
  digitalWrite(DIR_PIN, DIR_CCW);
  stepperDir = DIR_CCW;
  delayMicroseconds(5);
  while (digitalRead(LIMIT_PIN) == HIGH) {
    stepHomingPulse();
  }

  stepperPosition = 0;
  digitalWrite(DIR_PIN, DIR_CW);
  stepperDir = DIR_CW;
}

void updateDisplay(int dial, long posMm, unsigned long spdMmPerSec) {
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

  display.setCursor(0, 33);
  display.print(F("Pos:"));
  display.print(posMm);
  display.print(F("mm Spd:"));
  display.print(spdMmPerSec);
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
  digitalWrite(STEP_PIN, LOW);

  Wire.begin();
  Wire.setClock(400000);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
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

  homeStepper();
  lastStepUs = micros();
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
      if (encSwState == LOW) encoderPos = 0;
    }
  }
  lastEncSwReading = reading;

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
      // Drift recovery: if we tripped the switch near home, re-zero.
      if (stepperPosition < MIN_POSITION_STEPS + 200) stepperPosition = 0;
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
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(3);
      digitalWrite(STEP_PIN, LOW);
      if (stepperDir == DIR_CW) stepperPosition++;
      else stepperPosition--;
      lastStepUs = nowUs;
    }
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    // Speed reflects what the motor is actually doing: zero when edge-
    // stopped or dial at zero, otherwise the target rate derived from
    // the current step interval.
    unsigned long spdMmPerSec = 0;
    if (wantMove) {
      spdMmPerSec = 1000000UL /
                    (targetInterval * (unsigned long)STEPS_PER_MM);
    }
    updateDisplay(dial, stepperPosition / STEPS_PER_MM, spdMmPerSec);
    lastDisplayUpdate = millis();
  }
}
