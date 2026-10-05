// Standalone bring-up sketch for the Waveshare MLX90640-D110 thermal
// camera on the Arduino Nano ESP32. Streams frames over USB serial so
// the raw thermal image can be checked without the motor firmware, the
// Worker, or any network traffic. See THERMAL_CAMERA.md for wiring and
// the step-by-step test.
//
// The camera sits on its own I2C bus (Wire1, SDA = A0, SCL = A1) so it
// never shares a bus with the OLED on Wire (A4/A5) once it is merged
// into the main firmware.
//
// Two output modes:
//   Summary (default): one human-readable line per second, for the
//                      Arduino Serial Monitor.
//   Frames:            one machine-readable line per frame, for
//                      tools/thermal-viewer/thermal-serial.html.
//
// Serial commands (newline-terminated, case-insensitive):
//   F      switch to frame mode
//   T      switch to summary (text) mode
//   R<hz>  sensor refresh rate: 1, 2, 4, 8, 16, 32 (full frames/s = hz / 2)
//   I      print camera info
//
// Frame line format (all temperatures in tenths of a degree C):
//   F,<seq>,<millis>,<min>,<max>,<avg>,<p0>,<p1>,...,<p767>
// Pixels are row-major, 32 columns x 24 rows, as returned by the
// Adafruit library (index = row * 32 + col). Lines starting with "#"
// are informational.

#include <Wire.h>

#include <Adafruit_MLX90640.h>

namespace pins {
const int kThermalSda = A0;
const int kThermalScl = A1;
}  // namespace pins

const uint8_t kCameraAddr = MLX90640_I2CADDR_DEFAULT;  // 0x33
const int kCols = 32;
const int kRows = 24;
const int kPixels = kCols * kRows;

// 8 Hz sensor refresh = ~4 full frames/s: smooth enough to see motion
// while keeping noise around 0.3 C.
const int kDefaultRefreshHz = 8;

// Waveshare rates the module for 1 MHz I2C, but warns that long wires
// cause errors. Stay at 400 kHz unless a fast refresh rate needs more
// bandwidth.
const uint32_t kI2cStandardHz = 400000;
const uint32_t kI2cFastHz = 1000000;

const unsigned long kSummaryIntervalMs = 1000;
const unsigned long kRetryIntervalMs = 3000;

Adafruit_MLX90640 camera;
float frame[kPixels];

bool cameraReady = false;
bool frameMode = false;
int refreshHz = kDefaultRefreshHz;
uint32_t frameSeq = 0;
uint32_t framesSinceSummary = 0;
uint32_t readErrors = 0;
unsigned long lastSummaryMs = 0;
unsigned long lastRetryMs = 0;

char cmdBuf[16];
size_t cmdLen = 0;

bool refreshRateEnum(int hz, mlx90640_refreshrate_t* out) {
  switch (hz) {
    case 1:  *out = MLX90640_1_HZ;  return true;
    case 2:  *out = MLX90640_2_HZ;  return true;
    case 4:  *out = MLX90640_4_HZ;  return true;
    case 8:  *out = MLX90640_8_HZ;  return true;
    case 16: *out = MLX90640_16_HZ; return true;
    case 32: *out = MLX90640_32_HZ; return true;
    default: return false;
  }
}

void applyRefreshRate(int hz) {
  mlx90640_refreshrate_t rate;
  if (!refreshRateEnum(hz, &rate)) {
    Serial.printf("# ERROR unsupported rate %d Hz (use 1, 2, 4, 8, 16, 32)\n", hz);
    return;
  }
  refreshHz = hz;
  Wire1.setClock(hz >= 16 ? kI2cFastHz : kI2cStandardHz);
  if (cameraReady) camera.setRefreshRate(rate);
  Serial.printf("# rate=%dHz (~%.1f fps) i2c=%luHz\n", refreshHz, refreshHz / 2.0f,
                (unsigned long)(hz >= 16 ? kI2cFastHz : kI2cStandardHz));
}

void scanBus() {
  Serial.print("# Wire1 scan (SDA=A0, SCL=A1):");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) {
      Serial.printf(" 0x%02X", addr);
      found++;
    }
  }
  Serial.println(found ? "" : " no devices found - check wiring and power");
}

void printInfo() {
  if (!cameraReady) {
    Serial.println("# camera not ready");
    return;
  }
  Serial.printf("# MLX90640 serial %04X%04X%04X at 0x%02X\n", camera.serialNumber[0],
                camera.serialNumber[1], camera.serialNumber[2], kCameraAddr);
  Serial.printf("# mode=%s rate=%dHz read_errors=%lu\n", frameMode ? "frames" : "summary",
                refreshHz, (unsigned long)readErrors);
}

bool startCamera() {
  if (!camera.begin(kCameraAddr, &Wire1)) {
    Serial.printf("# ERROR MLX90640 not found at 0x%02X\n", kCameraAddr);
    scanBus();
    return false;
  }
  camera.setMode(MLX90640_CHESS);
  camera.setResolution(MLX90640_ADC_18BIT);
  cameraReady = true;
  applyRefreshRate(refreshHz);
  printInfo();
  return true;
}

void handleCommand(const char* cmd) {
  switch (toupper(cmd[0])) {
    case 'F':
      frameMode = true;
      Serial.println("# mode=frames");
      break;
    case 'T':
      frameMode = false;
      framesSinceSummary = 0;
      lastSummaryMs = millis();
      Serial.println("# mode=summary");
      break;
    case 'R':
      applyRefreshRate(atoi(cmd + 1));
      break;
    case 'I':
      printInfo();
      break;
    default:
      Serial.printf("# unknown command '%s' (F, T, R<hz>, I)\n", cmd);
  }
}

void readCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        handleCommand(cmdBuf);
        cmdLen = 0;
      }
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

// Tenths of a degree, with -9999 marking an invalid pixel (NaN / inf).
int toDeci(float t) {
  if (isnan(t) || isinf(t)) return -9999;
  return (int)lroundf(t * 10.0f);
}

void emitFrame(float minT, float maxT, float avgT) {
  // Build the whole line first so it reaches the host as one write.
  static char line[kPixels * 7 + 64];
  int n = snprintf(line, sizeof(line), "F,%lu,%lu,%d,%d,%d", (unsigned long)frameSeq,
                   (unsigned long)millis(), toDeci(minT), toDeci(maxT), toDeci(avgT));
  for (int i = 0; i < kPixels && n < (int)sizeof(line) - 8; i++) {
    n += snprintf(line + n, sizeof(line) - n, ",%d", toDeci(frame[i]));
  }
  line[n++] = '\n';
  Serial.write((const uint8_t*)line, n);
}

void emitSummary(float minT, float maxT, float avgT, int hotIndex) {
  unsigned long now = millis();
  if (now - lastSummaryMs < kSummaryIntervalMs) return;
  float fps = framesSinceSummary * 1000.0f / (now - lastSummaryMs);
  Serial.printf("min %5.1f C  max %5.1f C  avg %5.1f C  hottest x=%2d y=%2d  fps %.1f\n", minT,
                maxT, avgT, hotIndex % kCols, hotIndex / kCols, fps);
  lastSummaryMs = now;
  framesSinceSummary = 0;
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // give the USB serial port time to enumerate
  Serial.println("# EmberSensor thermal USB test (MLX90640)");

  Wire1.begin(pins::kThermalSda, pins::kThermalScl);
  Wire1.setClock(kI2cStandardHz);
  startCamera();
  lastRetryMs = millis();
  lastSummaryMs = millis();
}

void loop() {
  readCommands();

  if (!cameraReady) {
    if (millis() - lastRetryMs >= kRetryIntervalMs) {
      lastRetryMs = millis();
      startCamera();
    }
    return;
  }

  // Blocks until both chess-pattern subpages have been read
  // (~250 ms at 8 Hz).
  if (camera.getFrame(frame) != 0) {
    readErrors++;
    Serial.printf("# WARN frame read failed (errors=%lu)\n", (unsigned long)readErrors);
    return;
  }
  frameSeq++;
  framesSinceSummary++;

  float minT = INFINITY, maxT = -INFINITY, sum = 0;
  int valid = 0, hotIndex = 0;
  for (int i = 0; i < kPixels; i++) {
    float t = frame[i];
    if (isnan(t) || isinf(t)) continue;
    if (t < minT) minT = t;
    if (t > maxT) {
      maxT = t;
      hotIndex = i;
    }
    sum += t;
    valid++;
  }
  float avgT = valid ? sum / valid : NAN;

  if (frameMode) {
    emitFrame(minT, maxT, avgT);
  } else {
    emitSummary(minT, maxT, avgT, hotIndex);
  }
}
