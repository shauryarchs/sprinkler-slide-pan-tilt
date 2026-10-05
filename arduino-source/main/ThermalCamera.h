#pragma once

#include <Arduino.h>

class TiltMotor3;

// Smoothed hotspot state, safe to read from any task via
// ThermalCamera::snapshot(). Angles are relative to the image center:
// dxDeg > 0 = heat is right of center, dyDeg > 0 = heat is above center
// (camera's point of view, after the kFlipX / kFlipY corrections).
struct ThermalSnapshot {
  bool cameraOk = false;        // camera detected and frames arriving
  bool hotspot = false;         // debounced "heat detected"
  float dxDeg = 0;              // smoothed horizontal offset of the hot blob
  float dyDeg = 0;              // smoothed vertical offset of the hot blob
  float maxC = NAN;             // smoothed hottest-pixel temperature
  float backgroundC = NAN;      // median scene temperature
  int blobPixels = 0;           // size of the hot blob (pixels)
  float distanceM = NAN;        // flat-ground estimate, NAN when unavailable
  unsigned long frames = 0;     // frames processed since boot
  unsigned long readErrors = 0;
};

// MLX90640 thermal camera running in a background FreeRTOS task.
//
// The camera has its own I2C bus (Wire1, SDA = A0, SCL = A1) so it never
// contends with the OLED on Wire. The task is pinned to core 0 next to
// StatePusher / CommandPoller, keeping frame reads and the per-frame
// math off the core that runs the motor pulse timers and encoder ISR.
//
// Each frame: find the hottest pixel, grow the hot blob around it,
// take its temperature-weighted centroid, convert to degrees from the
// image center, then smooth over ~1 s and debounce detection so flame
// flicker doesn't make the output jump. Status is printed to the USB
// Serial Monitor once per second as a line starting with "THERMAL".
//
// Display only: nothing here moves a motor or switches water.
class ThermalCamera {
 public:
  // --- Detection tuning -------------------------------------------------
  // A pixel only counts as a hotspot when it is at least kHotspotMinC
  // AND at least kMinRiseC above the scene's median. 50 C keeps people
  // (skin ~33-36 C) from ever counting as fire; lower it temporarily
  // (e.g. to 30) to test detection with a hand.
  static constexpr float kHotspotMinC = 50.0f;
  static constexpr float kMinRiseC = 15.0f;
  // Consecutive frames needed to turn detection on / off (~4 fps).
  static constexpr int kFramesToDetect = 3;   // ~0.75 s
  static constexpr int kFramesToClear = 8;    // ~2 s
  // Exponential smoothing weight for each new frame (0..1). 0.4 at
  // ~4 fps settles in about 1 s.
  static constexpr float kSmoothing = 0.4f;

  // --- Orientation -------------------------------------------------------
  // Flip if a heat source to the camera's right reports dx < 0, or a
  // source above center reports dy < 0, for how the module is mounted.
  // kFlipX = true: hardware test (2026-10-05) showed the raw image is
  // mirrored left-right — a source on the camera's right read dx < 0.
  // Up/down was already correct.
  static constexpr bool kFlipX = true;
  static constexpr bool kFlipY = false;

  // --- Distance estimate (experimental, off by default) ------------------
  // Flat-ground estimate: distance = height / tan(angle below horizontal).
  // Needs the camera's height above the ground and the tilt reading at
  // which the camera points level. Leave kCameraHeightM at 0 to disable.
  static constexpr float kCameraHeightM = 0.0f;
  static constexpr float kTiltLevelDeg = 0.0f;
  static constexpr bool kTiltUpIsPositive = true;

  // --- Sensor / task -----------------------------------------------------
  static constexpr uint8_t kAddr = 0x33;
  static constexpr uint32_t kI2cHz = 400000;
  static constexpr int kCols = 32;
  static constexpr int kRows = 24;
  static constexpr int kPixels = kCols * kRows;
  static constexpr float kFovXDeg = 110.0f;  // MLX90640-D110
  static constexpr float kFovYDeg = 75.0f;
  static constexpr unsigned long kReportIntervalMs = 1000;
  static constexpr unsigned long kRetryIntervalMs = 10000;
  // Sleep between frames so the library's ready-poll doesn't spin on
  // core 0 (8 Hz sensor refresh = one subpage every 125 ms).
  static constexpr unsigned long kInterFrameSleepMs = 100;
  static constexpr int kStackBytes = 12288;
  static constexpr UBaseType_t kPriority = 1;
  static constexpr BaseType_t kCore = 0;

  explicit ThermalCamera(TiltMotor3& tilt);

  // Start Wire1 and the background task. Safe with no camera attached:
  // the task logs once and keeps retrying quietly.
  void begin();

  // Thread-safe copy of the latest smoothed state.
  ThermalSnapshot snapshot();

 private:
  static void taskTrampoline(void* arg);
  void run();
  bool tryStart();
  void processFrame();
  void report();

  TiltMotor3& tilt_;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  ThermalSnapshot state_;  // guarded by lock_

  // Task-only working state.
  bool started_ = false;
  bool loggedMissing_ = false;
  int hotFrames_ = 0;
  int coldFrames_ = 0;
  bool smoothValid_ = false;
  unsigned long lastReportMs_ = 0;
  bool lastReportedHotspot_ = false;
};
