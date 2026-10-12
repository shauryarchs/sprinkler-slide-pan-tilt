#include "AutoAim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "LimitSwitch.h"
#include "PanMotor2.h"
#include "SliderMotor1.h"
#include "ThermalCamera.h"
#include "TiltMotor3.h"

namespace {

constexpr float kPanStepsPerDeg = PanMotor2::kStepsPerRev / 360.0f;
constexpr float kTiltStepsPerDeg = TiltMotor3::kStepsPerRev / 360.0f;
constexpr float kSlideStepsPerMm = static_cast<float>(SliderMotor1::kStepsPerMm);

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
int signOf(float v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

}  // namespace

// ---------------------------------------------------------------------
// Planner
// ---------------------------------------------------------------------

bool aim::tiltInverted(float tiltDeg) {
  const float a = std::fmod(std::fabs(tiltDeg), 360.0f);
  return a > 90.0f && a < 270.0f;
}

AimPlan aim::planCorrection(const AimInputs& in) {
  AimPlan p;
  const bool needX = std::fabs(in.dxDeg) > kDeadbandXDeg;
  const bool needY = std::fabs(in.dyDeg) > kDeadbandYDeg;
  if (!needX && !needY) {
    p.onTarget = true;
    return p;
  }

  if (needX) {
    // Hotspot angle from the rail's outward direction ("straight out"),
    // as seen from the current carriage position. This is what dx would
    // be with pan at 0°. Facing backward (tilted past 90°) reverses which
    // way pan turns the view.
    const int panView = in.panSign * (tiltInverted(in.tiltDeg) ? -1 : 1);
    const float bearing = in.dxDeg + panView * in.panDeg;
    const bool panAtZero = std::fabs(in.panDeg) <= kPanZeroTolDeg;
    bool handled = false;

    // 1. Slide first, if the slider has room toward the hotspot. Pan
    //    goes back to 0° before sliding. Direction is fixed (slideSign);
    //    only the distance is learned.
    const int dir = signOf(panAtZero ? in.dxDeg : bearing) * in.slideSign;
    const float room = dir > 0 ? in.slideRoomPlusMm : in.slideRoomMinusMm;
    if (!in.slideIneffective && room >= kSlideMinMm) {
      if (!panAtZero && std::fabs(bearing) <= kReturnPanMaxBearingDeg) {
        p.panDeg = -in.panDeg;  // return to 0° in one move, then slide
        p.panReturn = true;
      } else {
        const float err = panAtZero ? std::fabs(in.dxDeg) : std::fabs(bearing);
        float mag;
        if (!std::isnan(in.dxPerMm) && in.dxPerMm > 0.0f) {
          mag = err / in.dxPerMm * kGain;  // measured: size the slide
        } else {
          mag = in.slideProbeMm;  // first probe
        }
        mag = std::min(clampf(mag, kSlideMinMm, kSlideMaxMm), room);
        p.slideMm = dir * mag;
      }
      handled = true;
    }

    // 2. Pan to aim only when sliding can't reach: no room toward the
    //    hotspot (beyond the end of the rail) or sliding has no effect.
    if (!handled) {
      float d = clampf(panView * in.dxDeg * kGain, -kMaxPanStepDeg, kMaxPanStepDeg);
      const float proom = d > 0 ? in.panRoomPlusDeg : in.panRoomMinusDeg;
      if (std::fabs(d) > proom) d = d > 0 ? proom : -proom;
      if (std::fabs(d) < kMinPanStepDeg) {
        d = 0;
        p.horizontalBlocked = true;
      }
      p.panDeg = d;
    }
  }

  // 3. Tilt for up/down. Tilt turns the view toward the camera's own "up"
  //    in either orientation, so its sign never changes.
  if (needY) {
    float d = clampf(in.tiltSign * in.dyDeg * kGain, -kMaxTiltStepDeg, kMaxTiltStepDeg);
    const float room = d > 0 ? in.tiltRoomPlusDeg : in.tiltRoomMinusDeg;
    if (std::fabs(d) > room) d = d > 0 ? room : -room;
    if (std::fabs(d) < kMinTiltStepDeg) {
      d = 0;
      p.verticalBlocked = true;
    }
    p.tiltDeg = d;
  }
  return p;
}

float aim::distanceFromSlide(float dxPerMm, float dxDeg) {
  // Bearing to a target at lateral offset X and range D: theta = atan(X/D),
  // so dtheta/dX = cos^2(theta) / D  =>  D = cos^2(theta) / (dtheta/dX).
  if (std::isnan(dxPerMm) || std::fabs(dxPerMm) < 1e-6f) return NAN;
  const float radPerMm = std::fabs(dxPerMm) * DEG_TO_RAD;
  const float c = std::cos(dxDeg * DEG_TO_RAD);
  return (c * c / radPerMm) / 1000.0f;
}

// ---------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------

AutoAim::AutoAim(SliderMotor1& slider, PanMotor2& pan, TiltMotor3& tilt, LimitSwitch& limit,
                 ThermalCamera& thermal)
    : slider_(slider), pan_(pan), tilt_(tilt), limit_(limit), thermal_(thermal) {}

float AutoAim::sliderMm() const { return slider_.positionSteps() / kSlideStepsPerMm; }
float AutoAim::panDeg() const { return pan_.positionSteps() / kPanStepsPerDeg; }
float AutoAim::tiltDeg() const { return tilt_.positionSteps() / kTiltStepsPerDeg; }

long AutoAim::axisPos(int axis) const {
  switch (axis) {
    case kSlide: return slider_.positionSteps();
    case kPan:   return pan_.positionSteps();
    default:     return tilt_.positionSteps();
  }
}

void AutoAim::axisDrive(int axis, int dial) {
  switch (axis) {
    case kSlide: slider_.update(dial, limit_); break;
    case kPan:   pan_.update(dial); break;
    default:     tilt_.update(dial); break;
  }
}

void AutoAim::axisStop(int axis) {
  switch (axis) {
    case kSlide: slider_.stop(); break;
    case kPan:   pan_.stop(); break;
    default:     tilt_.stop(); break;
  }
}

const char* AutoAim::stateName() const {
  switch (state_) {
    case State::Off:      return "off";
    case State::Waiting:  return "waiting";
    case State::Settling: return "settling";
    case State::Moving:   return "moving";
    case State::OnTarget: return "on-target";
    case State::LostHold: return "lost-hold";
    case State::Paused:   return "paused";
  }
  return "?";
}

void AutoAim::setEnabled(bool on) {
  if (on == enabled_) {
    printStatus();
    return;
  }
  if (on) {
    enabled_ = true;
    state_ = State::Waiting;
    stateSinceMs_ = millis();
    Serial.println("AIM on (no water yet). Acts only at the menu; any input pauses it. "
                   "Type 'aim off' to disable until reboot.");
  } else {
    stopAll();
    enabled_ = false;
    state_ = State::Off;
    Serial.println("AIM off");
  }
}

void AutoAim::pollSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c != '\n' && c != '\r') {
      if (serialLen_ < sizeof(serialBuf_) - 1) {
        serialBuf_[serialLen_++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      }
      continue;
    }
    if (serialLen_ == 0) continue;
    serialBuf_[serialLen_] = '\0';
    serialLen_ = 0;
    if (strcmp(serialBuf_, "aim on") == 0) {
      setEnabled(true);
    } else if (strcmp(serialBuf_, "aim off") == 0) {
      setEnabled(false);
    } else if (strcmp(serialBuf_, "aim status") == 0 || strcmp(serialBuf_, "aim") == 0) {
      printStatus();
    } else {
      Serial.println("Commands: aim on | aim off | aim status");
    }
  }
}

void AutoAim::printStatus() {
  const ThermalSnapshot s = thermal_.snapshot();
  Serial.printf("AIM status=%s enabled=%s atMenu=%s slider=%.0fmm pan=%.1fdeg tilt=%.1fdeg "
                "hotspot=%s dx=%+.1f dy=%+.1f panSign=%d tiltSign=%d\n",
                stateName(), enabled_ ? "yes" : "no", atMenu_ ? "yes" : "no", sliderMm(),
                panDeg(), tiltDeg(), s.hotspot ? "yes" : "no", s.dxDeg, s.dyDeg, panSign_,
                tiltSign_);
  if (enabled_ && !atMenu_) {
    Serial.println("AIM note: only acts at the menu - finish the pan/tilt setup screens or "
                   "leave the motor mode");
  }
}

void AutoAim::notifyUserActivity() {
  if (!enabled_) return;
  const bool wasActive = state_ != State::Paused && state_ != State::Waiting;
  stopAll();
  state_ = State::Paused;
  pausedUntilMs_ = millis() + kUserPauseMs;
  if (wasActive) Serial.println("AIM paused (user input)");
}

void AutoAim::stopAll() {
  for (int i = 0; i < kAxes; i++) {
    if (moving_[i]) {
      axisStop(i);
      moving_[i] = false;
    }
  }
}

void AutoAim::update(bool idle) {
  if (!announced_) {
    announced_ = true;
    if (enabled_) {
      Serial.println("AIM on at boot (no water yet). Acts only at the menu; any input pauses it. "
                     "Type 'aim off' to disable until reboot.");
    }
  }
  atMenu_ = idle;
  if (!enabled_) return;

  if (!idle) {
    // A motor mode or setup screen owns the motors; never fight it.
    stopAll();
    if (state_ != State::Paused) state_ = State::Waiting;
    return;
  }

  const unsigned long now = millis();
  switch (state_) {
    case State::Off:
      break;

    case State::Paused:
      if (static_cast<long>(now - pausedUntilMs_) >= 0) {
        state_ = State::Waiting;
      }
      break;

    case State::Waiting: {
      const ThermalSnapshot s = thermal_.snapshot();
      if (s.cameraOk && s.hotspot) startEngagement();
      break;
    }

    case State::Settling: {
      const ThermalSnapshot s = thermal_.snapshot();
      if (s.frames >= settleStartFrames_ + kSettleFrames && now - stateSinceMs_ >= kSettleMinMs) {
        evaluate();
      }
      break;
    }

    case State::Moving:
      driveAxes();
      if (!moving_[kSlide] && !moving_[kPan] && !moving_[kTilt]) {
        beginSettle();
      } else if (now - stateSinceMs_ > kMoveTimeoutMs) {
        stopAll();
        Serial.println("AIM move timed out; stopping");
        beginSettle();
      }
      break;

    case State::OnTarget: {
      const ThermalSnapshot s = thermal_.snapshot();
      if (!s.hotspot) {
        state_ = State::LostHold;
        stateSinceMs_ = now;
        Serial.println("AIM hotspot not visible, holding position");
      } else if (std::fabs(s.dxDeg) > aim::kDeadbandXDeg * kRetargetFactor ||
                 std::fabs(s.dyDeg) > aim::kDeadbandYDeg * kRetargetFactor) {
        Serial.println("AIM target moved, re-aiming");
        steps_ = 0;
        beginSettle();
      }
      break;
    }

    case State::LostHold: {
      const ThermalSnapshot s = thermal_.snapshot();
      if (s.hotspot) {
        beginSettle();
      } else if (now - stateSinceMs_ >= kLostHoldMs) {
        Serial.println("AIM lost, back to waiting");
        state_ = State::Waiting;
      }
      break;
    }
  }
}

void AutoAim::startEngagement() {
  steps_ = 0;
  dxPerMm_ = NAN;
  slideIneffective_ = false;
  slideProbeMm_ = aim::kSlideProbeMm;
  for (int i = 0; i < kAxes; i++) lastMoved_[i] = false;
  Serial.println("AIM hotspot acquired");
  beginSettle();
}

void AutoAim::beginSettle() {
  thermal_.restartSmoothing();
  settleStartFrames_ = thermal_.snapshot().frames;
  state_ = State::Settling;
  stateSinceMs_ = millis();
}

AimInputs AutoAim::gatherInputs(float dx, float dy) const {
  AimInputs in;
  in.dxDeg = dx;
  in.dyDeg = dy;
  in.panDeg = panDeg();
  in.tiltDeg = tiltDeg();

  const long sp = slider_.positionSteps();
  in.slideRoomPlusMm = std::max(0L, SliderMotor1::kMaxPositionSteps - sp) / kSlideStepsPerMm;
  in.slideRoomMinusMm =
      limit_.engaged() ? 0.0f
                       : std::max(0L, sp - SliderMotor1::kMinPositionSteps) / kSlideStepsPerMm;

  const long pp = pan_.positionSteps();
  in.panRoomPlusDeg = std::max(0L, PanMotor2::kMaxPositionSteps - pp) / kPanStepsPerDeg;
  in.panRoomMinusDeg = std::max(0L, pp - PanMotor2::kMinPositionSteps) / kPanStepsPerDeg;

  const long tp = tilt_.positionSteps();
  in.tiltRoomPlusDeg = std::max(0L, TiltMotor3::kMaxPositionSteps - tp) / kTiltStepsPerDeg;
  in.tiltRoomMinusDeg = std::max(0L, tp - TiltMotor3::kMinPositionSteps) / kTiltStepsPerDeg;

  in.dxPerMm = dxPerMm_;
  in.slideIneffective = slideIneffective_;
  in.slideProbeMm = slideProbeMm_;
  in.slideSign = kSlideSign;
  in.panSign = panSign_;
  in.tiltSign = tiltSign_;
  return in;
}

void AutoAim::evaluate() {
  const ThermalSnapshot s = thermal_.snapshot();
  if (!s.cameraOk) {
    Serial.println("AIM camera not available, waiting");
    state_ = State::Waiting;
    return;
  }
  if (!s.hotspot) {
    state_ = State::LostHold;
    stateSinceMs_ = millis();
    Serial.println("AIM hotspot not visible, holding position");
    return;
  }
  const float dx = s.dxDeg, dy = s.dyDeg;

  // --- Learn from the previous move ----------------------------------
  if (lastMoved_[kSlide]) {
    const float movedMm = (slider_.positionSteps() - beforePos_[kSlide]) / kSlideStepsPerMm;
    const float change = dx - beforeDx_;
    if (std::fabs(movedMm) >= 1.0f && std::fabs(change) >= kSlideNoEffectDeg) {
      dxPerMm_ = std::fabs(change / movedMm);  // size only; direction stays fixed
      const float dist = aim::distanceFromSlide(dxPerMm_, dx);
      if (!std::isnan(dist)) Serial.printf("AIM slide effect %.3f deg/mm, distance ~%.1fm (est)\n",
                                           dxPerMm_, dist);
    } else if (std::fabs(movedMm) >= kSlideIneffectiveMm) {
      slideIneffective_ = true;
      Serial.println("AIM sliding barely moves the view (distant target), using pan");
    } else if (std::isnan(dxPerMm_)) {
      slideProbeMm_ = std::min(slideProbeMm_ * 2.0f, aim::kSlideMaxMm);
      Serial.printf("AIM small slide effect, probing %.0fmm next\n", slideProbeMm_);
    }
  }
  if (lastMoved_[kPan] && !lastPanWasReturn_ &&
      std::fabs(dx) > std::fabs(beforeDx_) + kDivergeDeg) {
    if (panFlipped_) {
      Serial.println("AIM pan moved the target away again (moving target?), pausing");
      for (int i = 0; i < kAxes; i++) lastMoved_[i] = false;
      lastPanWasReturn_ = false;
      state_ = State::Paused;
      pausedUntilMs_ = millis() + kUserPauseMs;
      return;
    }
    panSign_ = -panSign_;
    panFlipped_ = true;
    Serial.printf("AIM pan direction reversed at runtime; set AutoAim::kPanSign = %d "
                  "to make it permanent\n", panSign_);
  }
  if (lastMoved_[kTilt] && std::fabs(dy) > std::fabs(beforeDy_) + kDivergeDeg) {
    if (tiltFlipped_) {
      Serial.println("AIM tilt moved the target away again (moving target?), pausing");
      for (int i = 0; i < kAxes; i++) lastMoved_[i] = false;
      lastPanWasReturn_ = false;
      state_ = State::Paused;
      pausedUntilMs_ = millis() + kUserPauseMs;
      return;
    }
    tiltSign_ = -tiltSign_;
    tiltFlipped_ = true;
    Serial.printf("AIM tilt direction reversed at runtime; set AutoAim::kTiltSign = %d "
                  "to make it permanent\n", tiltSign_);
  }
  for (int i = 0; i < kAxes; i++) lastMoved_[i] = false;
  lastPanWasReturn_ = false;

  // --- Decide the next move ------------------------------------------
  const AimPlan plan = aim::planCorrection(gatherInputs(dx, dy));
  if (plan.onTarget) {
    if (state_ != State::OnTarget) {
      char dist[16] = "n/a";
      const float d = aim::distanceFromSlide(dxPerMm_, dx);
      if (!std::isnan(d)) snprintf(dist, sizeof(dist), "~%.1fm(est)", d);
      Serial.printf("AIM on-target slider=%.0fmm pan=%.1fdeg tilt=%.1fdeg dist=%s\n", sliderMm(),
                    panDeg(), tiltDeg(), dist);
    }
    state_ = State::OnTarget;
    stateSinceMs_ = millis();
    return;
  }
  if (plan.slideMm == 0 && plan.panDeg == 0 && plan.tiltDeg == 0) {
    Serial.printf("AIM limit reached: can't center (%s%s), pausing\n",
                  plan.horizontalBlocked ? "left/right" : "",
                  plan.verticalBlocked ? (plan.horizontalBlocked ? ", up/down" : "up/down") : "");
    state_ = State::Paused;
    pausedUntilMs_ = millis() + kUserPauseMs;
    return;
  }
  if (++steps_ > kMaxStepsPerEngagement) {
    Serial.printf("AIM gave up after %d steps, pausing\n", kMaxStepsPerEngagement);
    state_ = State::Paused;
    pausedUntilMs_ = millis() + kUserPauseMs;
    return;
  }

  beforeDx_ = dx;
  beforeDy_ = dy;
  for (int i = 0; i < kAxes; i++) beforePos_[i] = axisPos(i);
  lastPanWasReturn_ = plan.panReturn;
  if (plan.panReturn) {
    Serial.printf("AIM step %d: returning pan to 0deg before sliding (pan%+.1fdeg tilt%+.1fdeg)\n",
                  steps_, plan.panDeg, plan.tiltDeg);
  } else {
    Serial.printf("AIM step %d: slide%+.0fmm pan%+.1fdeg tilt%+.1fdeg (dx=%+.1f dy=%+.1f)\n", steps_,
                  plan.slideMm, plan.panDeg, plan.tiltDeg, dx, dy);
  }
  startMove(plan);
}

void AutoAim::startMove(const AimPlan& plan) {
  const long delta[kAxes] = {
      lroundf(plan.slideMm * kSlideStepsPerMm),
      lroundf(plan.panDeg * kPanStepsPerDeg),
      lroundf(plan.tiltDeg * kTiltStepsPerDeg),
  };
  const unsigned long now = millis();
  for (int i = 0; i < kAxes; i++) {
    moving_[i] = delta[i] != 0;
    lastMoved_[i] = moving_[i];
    hitLimit_[i] = false;
    if (!moving_[i]) continue;
    target_[i] = axisPos(i) + delta[i];
    moveDir_[i] = delta[i] > 0 ? 1 : -1;
    lastProgressPos_[i] = axisPos(i);
    lastProgressMs_[i] = now;
  }
  state_ = State::Moving;
  stateSinceMs_ = now;
}

void AutoAim::driveAxes() {
  const unsigned long now = millis();
  for (int i = 0; i < kAxes; i++) {
    if (!moving_[i]) continue;
    const long pos = axisPos(i);
    const long remaining = target_[i] - pos;
    if (remaining == 0) {
      axisStop(i);
      moving_[i] = false;
      continue;
    }
    // Reached or passed the target (direction is fixed for the move).
    const int dir = moveDir_[i];
    if ((remaining > 0 ? 1 : -1) != dir) {
      axisStop(i);
      moving_[i] = false;
      continue;
    }

    float remainingUnits;
    bool near;
    int speed;
    if (i == kSlide) {
      remainingUnits = std::fabs(remaining / kSlideStepsPerMm);
      near = remainingUnits <= kNearSlideMm;
      speed = near ? kSlideSpeedNear : kSlideSpeed;
    } else {
      remainingUnits = std::fabs(remaining / (i == kPan ? kPanStepsPerDeg : kTiltStepsPerDeg));
      near = remainingUnits <= kNearAngleDeg;
      speed = near ? kPanTiltSpeedNear : kPanTiltSpeed;
    }
    axisDrive(i, dir * speed);

    // No progress for kStallMs: the motor's own limit stopped it.
    if (pos != lastProgressPos_[i]) {
      lastProgressPos_[i] = pos;
      lastProgressMs_[i] = now;
    } else if (now - lastProgressMs_[i] > kStallMs) {
      axisStop(i);
      moving_[i] = false;
      hitLimit_[i] = true;
      Serial.printf("AIM %s reached its limit\n", i == kSlide ? "slider" : (i == kPan ? "pan" : "tilt"));
    }
  }
}
