#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <util/atomic.h>

// LED moved to D11 (Timer2 PWM). Timer1 is reserved for the step ISR — D9/D10 PWM
// will not work while this sketch runs.
const int LED_PIN = 11;
const int BUTTON_PIN = 10;
const int POT_PIN = A0;
const int DIR_PIN = 2;
const int STEP_PIN = 3;
const int LIMIT_PIN = 4;

const int MIN_BRIGHTNESS = 15;
const int MAX_BRIGHTNESS = 255;
const int POT_MAX_ANGLE = 270;
// Asymmetric deadband: stopped between POT_STOP_LOW and POT_STOP_HIGH.
// Below POT_STOP_LOW → CCW. Above POT_STOP_HIGH → CW.
const int POT_STOP_LOW = 120;
const int POT_STOP_HIGH = 142;
// Speed scaling: pot deflection past either deadband edge maps linearly to
// the step interval. Capped at the smaller of the two available ranges
// (POT_STOP_LOW = 120° on the CCW side) so max CCW speed == max CW speed.
const int POT_MAX_MAGNITUDE = 120;

// 200 full-steps/rev * 32 microsteps (MS1=VDD, MS2=GND) = 6400 steps/rev.
// 160 steps/mm assumes GT2 belt + 20T pulley (40 mm/rev).
const long STEPS_PER_MM = 160;
// Auto-reverse points (carriage bounces between these in normal operation).
const long MAX_POSITION_MM = 500;
const long MIN_POSITION_MM = 5;
const long MAX_POSITION_STEPS = MAX_POSITION_MM * STEPS_PER_MM;
const long MIN_POSITION_STEPS = MIN_POSITION_MM * STEPS_PER_MM;

const unsigned long STEP_INTERVAL_MIN_US = 150;
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

const int POT_SAMPLES = 8;
int potBuffer[POT_SAMPLES] = {0};
int potIndex = 0;
long potSum = 0;
bool potBufferFilled = false;

int readPotSmoothed() {
  potSum -= potBuffer[potIndex];
  potBuffer[potIndex] = analogRead(POT_PIN);
  potSum += potBuffer[potIndex];
  potIndex = (potIndex + 1) % POT_SAMPLES;
  if (potIndex == 0) potBufferFilled = true;
  int divisor = potBufferFilled ? POT_SAMPLES : max(1, potIndex);
  return potSum / divisor;
}

ISR(TIMER1_COMPA_vect) {
  if (!stepperEnabled) return;
  // Hard edge stops: refuse any step that would push past either limit,
  // regardless of loop latency or decel ramp.
  if (stepperDir == DIR_CW && stepperPosition >= MAX_POSITION_STEPS) {
    stepperEnabled = false;
    return;
  }
  if (stepperDir == DIR_CCW &&
      (stepperPosition <= MIN_POSITION_STEPS || digitalRead(LIMIT_PIN) == LOW)) {
    stepperEnabled = false;
    return;
  }
  digitalWrite(STEP_PIN, HIGH);
  digitalWrite(STEP_PIN, LOW);
  if (stepperDir == DIR_CW) stepperPosition++;
  else stepperPosition--;
}

void setStepperInterval(unsigned long us) {
  // Timer1 prescaler 8 → 0.5 µs/tick. OCR1A counts ticks - 1.
  uint32_t ticks = us * 2;
  if (ticks > 65535) ticks = 65535;
  if (ticks < 20) ticks = 20;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    OCR1A = (uint16_t)(ticks - 1);
  }
}

void setupTimer1() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 65535;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS11);
  TIMSK1 |= (1 << OCIE1A);
  interrupts();
}

long getStepperPosition() {
  long pos;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { pos = stepperPosition; }
  return pos;
}

void setStepperPosition(long pos) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { stepperPosition = pos; }
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
  bool limitHit = (digitalRead(LIMIT_PIN) == LOW);
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
  setupTimer1();
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
