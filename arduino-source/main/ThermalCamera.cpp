#include "ThermalCamera.h"

#include <Adafruit_MLX90640.h>
#include <Wire.h>

#include <algorithm>
#include <cmath>

#include "TiltMotor3.h"

namespace {

// Camera on its own bus; pins use Arduino Nano ESP32 silkscreen labels.
const int kThermalSda = A0;
const int kThermalScl = A1;

// After this many consecutive failed reads, treat the camera as lost
// and go back to probing for it.
const int kMaxConsecutiveErrors = 10;

// Large buffers live in static storage, not on the task stack. Only the
// thermal task touches them.
Adafruit_MLX90640 camera;
float frame[ThermalCamera::kPixels];
float sorted[ThermalCamera::kPixels];
uint16_t fillStack[ThermalCamera::kPixels];
bool inBlob[ThermalCamera::kPixels];
int consecutiveErrors = 0;

bool validTemp(float t) { return !std::isnan(t) && !std::isinf(t); }

// True if something ACKs at the camera's address. Probing first avoids
// calling Adafruit_MLX90640::begin() repeatedly when nothing is wired:
// each begin() allocates an I2C device object that is never freed.
bool cameraPresent() {
  Wire1.beginTransmission(ThermalCamera::kAddr);
  return Wire1.endTransmission() == 0;
}

}  // namespace

ThermalCamera::ThermalCamera(TiltMotor3& tilt) : tilt_(tilt) {}

void ThermalCamera::begin() {
  Wire1.begin(kThermalSda, kThermalScl);
  Wire1.setClock(kI2cHz);
  xTaskCreatePinnedToCore(taskTrampoline, "thermal", kStackBytes, this, kPriority,
                          nullptr, kCore);
}

ThermalSnapshot ThermalCamera::snapshot() {
  portENTER_CRITICAL(&lock_);
  ThermalSnapshot copy = state_;
  portEXIT_CRITICAL(&lock_);
  return copy;
}

void ThermalCamera::taskTrampoline(void* arg) {
  static_cast<ThermalCamera*>(arg)->run();
}

void ThermalCamera::run() {
  unsigned long lastAttemptMs = 0;
  bool firstAttempt = true;
  for (;;) {
    if (!started_) {
      const unsigned long now = millis();
      if (firstAttempt || now - lastAttemptMs >= kRetryIntervalMs) {
        firstAttempt = false;
        lastAttemptMs = now;
        tryStart();
      }
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    // Blocks until both chess-pattern subpages are read (~250 ms at 8 Hz).
    if (camera.getFrame(frame) != 0) {
      portENTER_CRITICAL(&lock_);
      state_.readErrors++;
      portEXIT_CRITICAL(&lock_);
      if (++consecutiveErrors >= kMaxConsecutiveErrors) {
        Serial.println("THERMAL camera=lost (repeated read errors), retrying");
        portENTER_CRITICAL(&lock_);
        state_.cameraOk = false;
        state_.hotspot = false;
        portEXIT_CRITICAL(&lock_);
        started_ = false;
        loggedMissing_ = true;  // already reported; stay quiet while probing
      }
      vTaskDelay(pdMS_TO_TICKS(kInterFrameSleepMs));
      continue;
    }
    consecutiveErrors = 0;

    processFrame();
    report();
    vTaskDelay(pdMS_TO_TICKS(kInterFrameSleepMs));
  }
}

bool ThermalCamera::tryStart() {
  if (!cameraPresent() || !camera.begin(kAddr, &Wire1)) {
    if (!loggedMissing_) {
      Serial.println("THERMAL camera=missing (check VCC->3V3, GND, SDA->A0, SCL->A1); "
                     "motors unaffected, retrying every 10 s");
      loggedMissing_ = true;
    }
    return false;
  }
  camera.setMode(MLX90640_CHESS);
  camera.setResolution(MLX90640_ADC_18BIT);
  camera.setRefreshRate(MLX90640_8_HZ);  // ~4 full frames/s

  started_ = true;
  loggedMissing_ = false;
  consecutiveErrors = 0;
  hotFrames_ = coldFrames_ = 0;
  smoothValid_ = false;
  portENTER_CRITICAL(&lock_);
  state_.cameraOk = true;
  state_.hotspot = false;
  portEXIT_CRITICAL(&lock_);

  Serial.printf("THERMAL camera=ok serial=%04X%04X%04X rate=8Hz (~4 fps) "
                "threshold=%.0fC rise=%.0fC\n",
                camera.serialNumber[0], camera.serialNumber[1], camera.serialNumber[2],
                kHotspotMinC, kMinRiseC);
  return true;
}

void ThermalCamera::processFrame() {
  // Hottest pixel and median background.
  int valid = 0, hotIdx = -1;
  float maxT = -INFINITY;
  for (int i = 0; i < kPixels; i++) {
    const float t = frame[i];
    if (!validTemp(t)) continue;
    sorted[valid++] = t;
    if (t > maxT) {
      maxT = t;
      hotIdx = i;
    }
  }
  if (valid == 0) return;
  std::nth_element(sorted, sorted + valid / 2, sorted + valid);
  const float background = sorted[valid / 2];

  const bool hotThisFrame =
      maxT >= kHotspotMinC && (maxT - background) >= kMinRiseC;

  // Grow the blob connected to the hottest pixel (4-neighbour flood fill)
  // over pixels above the halfway point between background and peak, and
  // take its temperature-weighted centroid. A flickering flame changes
  // shape every frame, but its centroid moves far less than the single
  // hottest pixel.
  float cx = hotIdx % kCols, cy = hotIdx / kCols;
  int blobPixels = 0;
  if (hotThisFrame) {
    const float cut = background + 0.5f * (maxT - background);
    std::fill(inBlob, inBlob + kPixels, false);
    int top = 0;
    fillStack[top++] = hotIdx;
    inBlob[hotIdx] = true;
    float wSum = 0, xSum = 0, ySum = 0;
    while (top > 0) {
      const int i = fillStack[--top];
      const int x = i % kCols, y = i / kCols;
      const float w = frame[i] - cut;
      wSum += w;
      xSum += w * x;
      ySum += w * y;
      blobPixels++;
      const int neighbours[4] = {x > 0 ? i - 1 : -1, x < kCols - 1 ? i + 1 : -1,
                                 y > 0 ? i - kCols : -1, y < kRows - 1 ? i + kCols : -1};
      for (int n : neighbours) {
        if (n >= 0 && !inBlob[n] && validTemp(frame[n]) && frame[n] >= cut) {
          inBlob[n] = true;
          fillStack[top++] = n;
        }
      }
    }
    if (wSum > 0) {
      cx = xSum / wSum;
      cy = ySum / wSum;
    }
  }

  // Pixel position -> degrees from image center. Row 0 is the top of the
  // scene, so "up" is decreasing row.
  float dx = (cx - (kCols - 1) / 2.0f) * (kFovXDeg / kCols);
  float dy = ((kRows - 1) / 2.0f - cy) * (kFovYDeg / kRows);
  if (kFlipX) dx = -dx;
  if (kFlipY) dy = -dy;

  // Debounce detection.
  if (hotThisFrame) {
    hotFrames_++;
    coldFrames_ = 0;
  } else {
    coldFrames_++;
    hotFrames_ = 0;
  }

  portENTER_CRITICAL(&lock_);
  bool hotspot = state_.hotspot;
  portEXIT_CRITICAL(&lock_);
  if (!hotspot && hotFrames_ >= kFramesToDetect) hotspot = true;
  if (hotspot && coldFrames_ >= kFramesToClear) hotspot = false;

  // Exponential smoothing. Position only updates on frames that actually
  // contain the hotspot, so a brief flicker-out doesn't drag it to the
  // corner; it restarts fresh when a new detection begins.
  ThermalSnapshot s = snapshot();
  if (!smoothValid_ || (hotThisFrame && hotFrames_ == 1 && !s.hotspot)) {
    s.dxDeg = dx;
    s.dyDeg = dy;
    s.maxC = maxT;
    smoothValid_ = true;
  } else {
    if (hotThisFrame) {
      s.dxDeg += kSmoothing * (dx - s.dxDeg);
      s.dyDeg += kSmoothing * (dy - s.dyDeg);
    }
    s.maxC += kSmoothing * (maxT - s.maxC);
  }
  s.backgroundC = background;
  s.blobPixels = hotThisFrame ? blobPixels : 0;
  s.hotspot = hotspot;
  s.cameraOk = true;
  s.frames++;

  // Flat-ground distance: angle below horizontal = -(tilt above level +
  // blob offset above center).
  s.distanceM = NAN;
  if (kCameraHeightM > 0 && hotspot) {
    const float tiltAboveLevel =
        (kTiltUpIsPositive ? 1.0f : -1.0f) * (tilt_.positionDegrees() - kTiltLevelDeg);
    const float belowDeg = -(tiltAboveLevel + s.dyDeg);
    if (belowDeg > 1.0f) {
      s.distanceM = kCameraHeightM / tanf(belowDeg * DEG_TO_RAD);
    }
  }

  portENTER_CRITICAL(&lock_);
  // readErrors may have been bumped concurrently; keep the larger value.
  s.readErrors = std::max(s.readErrors, state_.readErrors);
  state_ = s;
  portEXIT_CRITICAL(&lock_);
}

void ThermalCamera::report() {
  const ThermalSnapshot s = snapshot();
  const unsigned long now = millis();
  const bool changed = s.hotspot != lastReportedHotspot_;
  if (!changed && now - lastReportMs_ < kReportIntervalMs) return;
  lastReportMs_ = now;
  lastReportedHotspot_ = s.hotspot;

  if (s.hotspot) {
    char dist[16];
    if (std::isnan(s.distanceM)) {
      snprintf(dist, sizeof(dist), "n/a");
    } else {
      snprintf(dist, sizeof(dist), "~%.1fm(est)", s.distanceM);
    }
    Serial.printf("THERMAL hotspot=yes dx=%+.1fdeg dy=%+.1fdeg max=%.1fC bg=%.1fC "
                  "blob=%dpx dist=%s\n",
                  s.dxDeg, s.dyDeg, s.maxC, s.backgroundC, s.blobPixels, dist);
  } else {
    Serial.printf("THERMAL hotspot=no max=%.1fC bg=%.1fC\n", s.maxC, s.backgroundC);
  }
}
