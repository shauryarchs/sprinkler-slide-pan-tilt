#pragma once

// Application-level state shared between main.ino and StatePusher.
//
// main.ino is the single writer for `mode`, `menuIndex`, and
// `sliderHomed`. StatePusher reads them from its background task. On
// the ESP32-S3, single-word loads of plain integer/enum types are
// atomic across cores, so we don't need a mutex around these reads —
// the reader may see a slightly stale value for one tick, which is
// fine for a state-mirror push.

enum class Mode {
  Menu,
  Motor1Control,
  Motor2Control,
  Motor3Control,
  AllMotorsControl,
  PanSetup,
  TiltSetup,
};

extern Mode mode;
extern int menuIndex;
extern bool sliderHomed;

// Stable lowercase machine-readable names the website's control page
// keys off (see docs/control.html → MODE_LABELS).
inline const char* modeName(Mode m) {
  switch (m) {
    case Mode::Menu:             return "menu";
    case Mode::Motor1Control:    return "motor1";
    case Mode::Motor2Control:    return "motor2";
    case Mode::Motor3Control:    return "motor3";
    case Mode::AllMotorsControl: return "allmotors";
    case Mode::PanSetup:         return "panSetup";
    case Mode::TiltSetup:        return "tiltSetup";
  }
  return "unknown";
}
