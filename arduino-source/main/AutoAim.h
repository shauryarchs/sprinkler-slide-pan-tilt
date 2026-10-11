#pragma once

#include <Arduino.h>

class SliderMotor1;
class PanMotor2;
class TiltMotor3;
class LimitSwitch;
class ThermalCamera;

// ---------------------------------------------------------------------
// Correction planner (pure function — no hardware, unit-testable).
//
// Given where the hotspot is in the thermal image and where the axes
// are, decide the next move. Horizontal error is fixed by SLIDING first;
// pan is only used when the slider has no room left in the needed
// direction, sliding no longer changes the view (distant fire), or the
// arm is panned too far for sliding to move the view sideways. Vertical
// error is fixed by tilt.
// ---------------------------------------------------------------------
struct AimInputs {
  float dxDeg = 0;            // hotspot offset, + = right of image center
  float dyDeg = 0;            // hotspot offset, + = above image center
  float panDeg = 0;           // current pan angle (0 = facing out from rail)
  float tiltDeg = 0;          // current tilt angle (0 = boot reference)
  float slideRoomPlusMm = 0;  // room left toward the far end (975 mm)
  float slideRoomMinusMm = 0; // room left toward the home switch
  float panRoomPlusDeg = 0;   // room left before pan's + soft limit
  float panRoomMinusDeg = 0;
  float tiltRoomPlusDeg = 0;  // room left before tilt's ±360° limit
  float tiltRoomMinusDeg = 0;
  // Measured effect of sliding: change in dx per +1 mm of slider travel.
  // NAN until a slide has been measured in this engagement.
  float dxPerMm = NAN;
  bool slideIneffective = false;  // slides barely move the view (far fire)
  float slideProbeMm = 50.0f;     // size of the next probe slide
  int slideProbeSign = 1;  // probe direction: +1 = slide + for a hotspot on the right
  int panSign = 1;         // +1 = positive pan turns the view right
  int tiltSign = 1;        // +1 = positive tilt turns the view up
};

struct AimPlan {
  bool onTarget = false;   // both errors inside the deadband
  float slideMm = 0;       // signed slider move (+ = toward far end)
  float panDeg = 0;        // signed pan move
  float tiltDeg = 0;       // signed tilt move
  bool horizontalBlocked = false;  // no slide room and no pan room left
  bool verticalBlocked = false;    // no tilt room left
};

namespace aim {
// Deadband ~1 thermal pixel (110°/32 horizontally, 75°/24 vertically).
constexpr float kDeadbandXDeg = 3.4f;
constexpr float kDeadbandYDeg = 3.1f;
// Move this fraction of the measured error each step (avoids overshoot).
constexpr float kGain = 0.7f;
constexpr float kMaxPanStepDeg = 10.0f;
constexpr float kMaxTiltStepDeg = 10.0f;
constexpr float kMinPanStepDeg = 0.5f;
constexpr float kMinTiltStepDeg = 0.5f;
// First slide of an engagement is a probe; its measured effect sizes
// later slides and gives a distance estimate. If a probe barely moves
// the view, the next one doubles (up to kSlideMaxMm) before giving up.
constexpr float kSlideProbeMm = 50.0f;
constexpr float kSlideMinMm = 5.0f;
constexpr float kSlideMaxMm = 300.0f;
// Sliding moves the view sideways only when the camera faces roughly
// perpendicular to the rail.
constexpr float kSlideMaxPanDeg = 15.0f;

// True when tilt has turned the camera to face backward (upside down).
bool tiltInverted(float tiltDeg);

AimPlan planCorrection(const AimInputs& in);

// Triangulated distance from the measured slide effect (meters), or NAN.
float distanceFromSlide(float dxPerMm, float dxDeg);
}  // namespace aim

// ---------------------------------------------------------------------
// Auto-aim state machine.
//
// OFF at every boot. Enabled from the Serial Monitor ("aim on"), and
// only acts while the device is idle at the menu. Moves are driven
// through each motor's own update(), so the existing soft limits and the
// slider limit switch always apply (tilt can never pass ±360° from its
// boot zero). Any user input pauses it. Never switches water.
// ---------------------------------------------------------------------
class AutoAim {
 public:
  // --- Motor direction constants (verify on hardware) -----------------
  // If a move makes the error grow, the firmware reverses that axis once
  // at runtime and prints which constant to change; a second divergence
  // turns auto-aim off. Slide direction is measured, so its constant
  // only sets the direction of the very first probe.
  static constexpr int kSlideProbeSign = 1;
  static constexpr int kPanSign = 1;
  static constexpr int kTiltSign = 1;

  // --- Motion ---------------------------------------------------------
  // Dial values on the existing [0, 20] speed scale.
  static constexpr int kSlideSpeed = 18;      // ~15 mm/s
  static constexpr int kSlideSpeedNear = 12;  // ~5 mm/s for the last few mm
  static constexpr int kPanTiltSpeed = 6;
  static constexpr int kPanTiltSpeedNear = 3;
  static constexpr float kNearSlideMm = 5.0f;
  static constexpr float kNearAngleDeg = 2.0f;
  static constexpr unsigned long kStallMs = 400;  // no progress = at a limit
  static constexpr unsigned long kMoveTimeoutMs = 30000;

  // --- Loop -----------------------------------------------------------
  static constexpr unsigned long kSettleFrames = 3;    // fresh frames after a move
  static constexpr unsigned long kSettleMinMs = 300;
  static constexpr unsigned long kLostHoldMs = 3000;
  static constexpr unsigned long kUserPauseMs = 5000;
  static constexpr int kMaxStepsPerEngagement = 25;
  // Error must grow by this much before a move counts as "wrong way".
  static constexpr float kDivergeDeg = 2.0f;
  // A slide that shifts dx by less than kSlideNoEffectDeg is too small
  // to measure: probe further. Only after a slide of at least
  // kSlideIneffectiveMm still shifts it that little is the fire too far
  // for sliding to help (~15 m+), and pan takes over.
  static constexpr float kSlideNoEffectDeg = 0.8f;
  static constexpr float kSlideIneffectiveMm = 200.0f;
  // Re-aim from on-target once the error exceeds deadband × this.
  static constexpr float kRetargetFactor = 1.5f;

  AutoAim(SliderMotor1& slider, PanMotor2& pan, TiltMotor3& tilt, LimitSwitch& limit,
          ThermalCamera& thermal);

  // Call from loop() every iteration. idle = device is at the menu.
  void update(bool idle);

  // Call when the user does anything (encoder, remote command): stops
  // any auto-aim motion and pauses for kUserPauseMs.
  void notifyUserActivity();

  // Reads "aim on" / "aim off" / "aim status" lines from Serial.
  void pollSerial();

  void setEnabled(bool on);
  bool enabled() const { return enabled_; }

 private:
  enum class State { Off, Waiting, Settling, Moving, OnTarget, LostHold, Paused };
  enum Axis { kSlide = 0, kPan = 1, kTilt = 2, kAxes = 3 };

  void startEngagement();
  void beginSettle();
  void evaluate();
  void startMove(const AimPlan& plan);
  void driveAxes();
  void stopAll();
  void printStatus();
  AimInputs gatherInputs(float dx, float dy) const;
  const char* stateName() const;

  float sliderMm() const;
  float panDeg() const;
  float tiltDeg() const;
  long axisPos(int axis) const;
  void axisDrive(int axis, int dial);
  void axisStop(int axis);

  SliderMotor1& slider_;
  PanMotor2& pan_;
  TiltMotor3& tilt_;
  LimitSwitch& limit_;
  ThermalCamera& thermal_;

  bool enabled_ = false;
  State state_ = State::Off;
  unsigned long stateSinceMs_ = 0;
  unsigned long settleStartFrames_ = 0;
  unsigned long pausedUntilMs_ = 0;

  // Per-engagement learning.
  int steps_ = 0;
  float dxPerMm_ = NAN;
  bool slideIneffective_ = false;
  float slideProbeMm_ = aim::kSlideProbeMm;
  int panSign_ = kPanSign;
  int tiltSign_ = kTiltSign;
  bool panFlipped_ = false;
  bool tiltFlipped_ = false;

  // Last move, for divergence checks and slide measurement.
  bool lastMoved_[kAxes] = {false, false, false};
  float beforeDx_ = 0;
  float beforeDy_ = 0;
  long beforePos_[kAxes] = {0, 0, 0};

  // Active move.
  bool moving_[kAxes] = {false, false, false};
  long target_[kAxes] = {0, 0, 0};
  int moveDir_[kAxes] = {0, 0, 0};  // +1 / -1, fixed for the whole move
  long lastProgressPos_[kAxes] = {0, 0, 0};
  unsigned long lastProgressMs_[kAxes] = {0, 0, 0};
  bool hitLimit_[kAxes] = {false, false, false};

  char serialBuf_[24];
  size_t serialLen_ = 0;
};
