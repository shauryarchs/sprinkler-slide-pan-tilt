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
// CW edge stop (carriage halts here in jog mode).
const long MAX_POSITION_MM = 1000;
const long MIN_POSITION_MM = 5;
const long MAX_POSITION_STEPS = MAX_POSITION_MM * STEPS_PER_MM;
const long MIN_POSITION_STEPS = MIN_POSITION_MM * STEPS_PER_MM;

const unsigned long HOMING_STEP_INTERVAL_US = 200;

const int DIR_CW = LOW;
const int DIR_CCW = HIGH;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// KY-040 SW is active LOW with internal pull-up. While held, the carriage
// jogs CW at homing speed; on release the motor stops.
int encSwState = HIGH;
int lastEncSwReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 100;

// Single-fixed-speed jog: step from loop() using micros() timing — same
// direct-pulse approach as homeStepper(), which drives the TMC2209 cleanly
// on this board.
long stepperPosition = 0;
uint8_t stepperDir = DIR_CW;
unsigned long lastStepUs = 0;

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

void updateDisplay(bool jogging, long posMm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(F("Ember Sensor"));

  display.setCursor(0, 10);
  display.println(F("www.embersensor.com"));

  display.setCursor(0, 22);
  display.print(F("State: "));
  display.print(jogging ? F("Jog CW") : F("Idle"));

  display.setCursor(0, 33);
  display.print(F("Pos: "));
  display.print(posMm);
  display.print(F(" mm"));

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
}

void loop() {
  int reading = digitalRead(ENCODER_SW);
  if (reading != lastEncSwReading) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    encSwState = reading;
  }
  lastEncSwReading = reading;

  bool jogPressed = (encSwState == LOW);
  bool jogging = false;

  if (jogPressed && stepperPosition < MAX_POSITION_STEPS) {
    if (stepperDir != DIR_CW) {
      stepperDir = DIR_CW;
      digitalWrite(DIR_PIN, DIR_CW);
      delayMicroseconds(5);
    }
    unsigned long nowUs = micros();
    if (nowUs - lastStepUs >= HOMING_STEP_INTERVAL_US) {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(3);
      digitalWrite(STEP_PIN, LOW);
      stepperPosition++;
      lastStepUs = nowUs;
    }
    jogging = true;
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    updateDisplay(jogging, stepperPosition / STEPS_PER_MM);
    lastDisplayUpdate = millis();
  }
}
