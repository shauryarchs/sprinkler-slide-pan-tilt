#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pins use Arduino Nano ESP32 silkscreen labels (D4/D6/D7/D10/D11/A0). The
// core resolves them to the right ESP32-S3 GPIOs, so wiring is identical to
// the original Uno pinout. Targets arduino-esp32 core 2.x (Nano ESP32's
// bundled package); uses the legacy timerBegin/timerAlarmWrite API.
const int LED_PIN = D11;       // GPIO 38
const int BUTTON_PIN = D10;    // GPIO 21
const int POT_PIN = A0;        // GPIO 1  (ADC1_CH0)
const int DIR_PIN = D6;        // GPIO 9
const int STEP_PIN = D7;       // GPIO 10
const int LIMIT_PIN = D4;      // GPIO 7

const int MIN_BRIGHTNESS = 15;
const int MAX_BRIGHTNESS = 255;
const int POT_MAX_ANGLE = 270;
// Symmetric deadband: stopped between POT_STOP_LOW and POT_STOP_HIGH.
// Below POT_STOP_LOW → CCW (max speed at 0°). Above POT_STOP_HIGH → CW
// (max speed at 270°). Both sides have 130° of usable travel.
const int POT_STOP_LOW = 130;
const int POT_STOP_HIGH = 140;
const int POT_MAX_MAGNITUDE = 130;

// 200 full-steps/rev * 32 microsteps (MS1=VDD, MS2=GND) = 6400 steps/rev.
// 160 steps/mm assumes GT2 belt + 20T pulley (40 mm/rev).
const long STEPS_PER_MM = 160;
// Auto-reverse points (carriage bounces between these in normal operation).
const long MAX_POSITION_MM = 1000;
const long MIN_POSITION_MM = 5;
const long MAX_POSITION_STEPS = MAX_POSITION_MM * STEPS_PER_MM;
const long MIN_POSITION_STEPS = MIN_POSITION_MM * STEPS_PER_MM;

const unsigned long STEP_INTERVAL_MIN_US = 120;
const unsigned long STEP_INTERVAL_MAX_US = 6000;
const unsigned long RAMP_US_PER_MS = 60;
const unsigned long HOMING_STEP_INTERVAL_US = 200;

const int DIR_CW = LOW;
const int DIR_CCW = HIGH;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Button-toggled pause: each press flips motorPaused. While paused, the motor
// is held stopped regardless of pot input.
bool motorPaused = false;
int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 100;

volatile long stepperPosition = 0;
volatile uint8_t stepperDir = DIR_CW;
volatile bool stepperEnabled = false;

unsigned long currentInterval = STEP_INTERVAL_MAX_US;
unsigned long lastRampMs = 0;

// True while the carriage is bouncing between MIN/MAX. Locks the motor's
// direction to the bounce sequence (overriding pot direction) until the
// motor stops (pot enters deadband).
bool autoReverse = false;

const int POT_SAMPLES = 16;
int potBuffer[POT_SAMPLES] = {0};
int potIndex = 0;
long potSum = 0;
bool potBufferFilled = false;

portMUX_TYPE stepperMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t *stepperTimer = NULL;

int readPotSmoothed() {
  potSum -= potBuffer[potIndex];
  potBuffer[potIndex] = analogRead(POT_PIN);
  potSum += potBuffer[potIndex];
  potIndex = (potIndex + 1) % POT_SAMPLES;
  if (potIndex == 0) potBufferFilled = true;
  int divisor = potBufferFilled ? POT_SAMPLES : max(1, potIndex);
  return potSum / divisor;
}

// arduino-esp32 v2.x marks digitalWrite/digitalRead as IRAM_ATTR, so they're
// safe to call from this timer ISR. Matches the homing pulse, which is known
// to work end-to-end (driver receives the steps).
void IRAM_ATTR onStepperTimer() {
  if (!stepperEnabled) return;
  // Hard edge stops: refuse any step that would push past either limit,
  // regardless of loop latency or decel ramp.
  if (stepperDir == DIR_CW && stepperPosition >= MAX_POSITION_STEPS) {
    stepperEnabled = false;
    return;
  }
  if (stepperDir == DIR_CCW && stepperPosition <= MIN_POSITION_STEPS) {
    stepperEnabled = false;
    return;
  }
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(2);
  digitalWrite(STEP_PIN, LOW);
  if (stepperDir == DIR_CW) stepperPosition++;
  else stepperPosition--;
}

void setStepperInterval(unsigned long us) {
  if (us < 10) us = 10;
  if (stepperTimer == NULL) return;
  // Timer ticks at 1 MHz, so the alarm value is the period in microseconds.
  timerAlarmWrite(stepperTimer, us, true);
}

void setupStepperTimer() {
  // Timer 0, prescaler 80 → 80 MHz APB / 80 = 1 MHz tick (1 µs).
  stepperTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(stepperTimer, &onStepperTimer, true);
  timerAlarmWrite(stepperTimer, STEP_INTERVAL_MAX_US, true);
  timerAlarmEnable(stepperTimer);
}

long getStepperPosition() {
  long pos;
  portENTER_CRITICAL(&stepperMux);
  pos = stepperPosition;
  portEXIT_CRITICAL(&stepperMux);
  return pos;
}

void setStepperPosition(long pos) {
  portENTER_CRITICAL(&stepperMux);
  stepperPosition = pos;
  portEXIT_CRITICAL(&stepperMux);
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

  setStepperPosition(0);
  digitalWrite(DIR_PIN, DIR_CW);
  stepperDir = DIR_CW;
}

void rampInterval(unsigned long target) {
  unsigned long now = millis();
  unsigned long elapsed = now - lastRampMs;
  if (elapsed == 0) return;
  unsigned long maxChange = elapsed * RAMP_US_PER_MS;
  if (currentInterval > target) {
    if (currentInterval - target > maxChange) currentInterval -= maxChange;
    else currentInterval = target;
  } else if (currentInterval < target) {
    if (target - currentInterval > maxChange) currentInterval += maxChange;
    else currentInterval = target;
  }
  lastRampMs = now;
}

void updateStepper(int potAngle) {
  bool requestStop = false;
  uint8_t potDir = stepperDir;
  int magnitude = 0;

  if (potAngle < POT_STOP_LOW) {
    potDir = DIR_CCW;
    magnitude = POT_STOP_LOW - potAngle;
  } else if (potAngle > POT_STOP_HIGH) {
    potDir = DIR_CW;
    magnitude = potAngle - POT_STOP_HIGH;
  } else {
    requestStop = true;
  }

  if (motorPaused) requestStop = true;

  long pos = getStepperPosition();
  // Debounce + position guard: the limit switch's only job after homing is
  // to recover from drift if the carriage actually arrives near home. Out in
  // the middle of the rail, a LOW reading is noise and must be ignored.
  static int limitConsecutiveLow = 0;
  const int LIMIT_DEBOUNCE = 5;
  const long LIMIT_TRUST_THRESHOLD = MIN_POSITION_STEPS + 200;
  if (digitalRead(LIMIT_PIN) == LOW) {
    if (limitConsecutiveLow < LIMIT_DEBOUNCE) limitConsecutiveLow++;
  } else {
    limitConsecutiveLow = 0;
  }
  bool limitHit = (limitConsecutiveLow >= LIMIT_DEBOUNCE) &&
                  (pos < LIMIT_TRUST_THRESHOLD);
  if (limitHit) {
    setStepperPosition(0);
    pos = 0;
  }
  bool atRightEdge = (pos >= MAX_POSITION_STEPS);
  bool atLeftEdge = (pos <= MIN_POSITION_STEPS) || limitHit;

  // Auto-reverse: motor was actively moving into an edge → flip direction
  // and lock into bounce mode until the pot enters the deadband.
  if (stepperDir == DIR_CW && atRightEdge) {
    stepperDir = DIR_CCW;
    digitalWrite(DIR_PIN, DIR_CCW);
    currentInterval = STEP_INTERVAL_MAX_US;
    lastRampMs = millis();
    autoReverse = true;
  } else if (stepperDir == DIR_CCW && atLeftEdge) {
    stepperDir = DIR_CW;
    digitalWrite(DIR_PIN, DIR_CW);
    currentInterval = STEP_INTERVAL_MAX_US;
    lastRampMs = millis();
    autoReverse = true;
  }

  // While bouncing, pot direction is ignored — the bounce sequence wins.
  uint8_t desiredDir = autoReverse ? stepperDir : potDir;

  // Pot pushing into an edge from a stopped state → stay stopped.
  if (desiredDir == DIR_CW && atRightEdge) requestStop = true;
  if (desiredDir == DIR_CCW && atLeftEdge) requestStop = true;

  // Stop request → halt pulses immediately and exit bounce mode.
  if (requestStop) {
    stepperEnabled = false;
    currentInterval = STEP_INTERVAL_MAX_US;
    lastRampMs = millis();
    autoReverse = false;
    return;
  }

  // Direction change → ramp the current speed down, then flip DIR_PIN.
  if (desiredDir != stepperDir) {
    rampInterval(STEP_INTERVAL_MAX_US);
    if (currentInterval >= STEP_INTERVAL_MAX_US) {
      stepperDir = desiredDir;
      digitalWrite(DIR_PIN, desiredDir);
    }
    setStepperInterval(currentInterval);
    stepperEnabled = true;
    return;
  }

  // Same direction → ramp toward the speed the pot is asking for.
  int magClamped = constrain(magnitude, 1, POT_MAX_MAGNITUDE);
  unsigned long targetInterval = map(magClamped, 1, POT_MAX_MAGNITUDE,
                                     STEP_INTERVAL_MAX_US, STEP_INTERVAL_MIN_US);
  rampInterval(targetInterval);
  setStepperInterval(currentInterval);
  stepperEnabled = true;
}

void updateDisplay(int angle, long posMm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(F("Ember Sensor"));

  display.setCursor(0, 10);
  display.println(F("www.embersensor.com"));

  display.setCursor(0, 22);
  display.print(F("Angle: "));
  display.print(angle);
  display.write(247);

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
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  digitalWrite(STEP_PIN, LOW);
  // Match AVR's 10-bit ADC so the existing 0-1023 → angle map is unchanged.
  analogReadResolution(10);
  analogWrite(LED_PIN, MAX_BRIGHTNESS);

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
  setupStepperTimer();
  lastRampMs = millis();
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) motorPaused = !motorPaused;
    }
  }
  lastButtonReading = reading;

  int potValue = readPotSmoothed();
  int potAngle = map(potValue, 0, 1023, 0, POT_MAX_ANGLE);

  updateStepper(potAngle);

  // LED brightness scales with step rate: fast → bright, slow → dim, stopped → off.
  if (stepperEnabled) {
    unsigned long clamped = constrain(currentInterval,
                                      STEP_INTERVAL_MIN_US, STEP_INTERVAL_MAX_US);
    int brightness = map(clamped, STEP_INTERVAL_MIN_US, STEP_INTERVAL_MAX_US,
                         MAX_BRIGHTNESS, MIN_BRIGHTNESS);
    analogWrite(LED_PIN, brightness);
  } else {
    analogWrite(LED_PIN, 0);
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    updateDisplay(potAngle, getStepperPosition() / STEPS_PER_MM);
    lastDisplayUpdate = millis();
  }
}
