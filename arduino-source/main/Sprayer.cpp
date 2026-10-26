#include "Sprayer.h"

#include <cstdlib>
#include <cstring>

#include "AutoAim.h"

Sprayer::Sprayer(int pumpPin, AutoAim& aim) : pin_(pumpPin), aim_(aim) {}

void Sprayer::begin() {
  // Pump off before anything else: set the level, then make it an output.
  digitalWrite(pin_, LOW);
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void Sprayer::start(Run run, unsigned long maxMs, const char* why) {
  const bool wasOn = pumpOn();
  run_ = run;
  startMs_ = millis();
  maxMs_ = maxMs;
  digitalWrite(pin_, HIGH);
  if (run == Run::Auto) autoRuns_++;
  Serial.printf("SPRAY %s (%s, max %lus)\n", wasOn ? "timer reset" : "pump on", why,
                maxMs / 1000);
}

void Sprayer::stop(const char* why) {
  digitalWrite(pin_, LOW);
  if (!pumpOn()) return;
  run_ = Run::None;
  Serial.printf("SPRAY pump off (%s, ran %.1fs)\n", why, (millis() - startMs_) / 1000.0f);
}

void Sprayer::setAuto(bool on) {
  autoEnabled_ = on;
  if (!on && run_ == Run::Auto) stop("spray off");
  Serial.println(on ? "SPRAY auto on: pump runs when auto-aim is on target. "
                      "Type 'spray off' to disable until reboot."
                    : "SPRAY auto off");
}

void Sprayer::notifyUserActivity() {
  if (pumpOn()) stop("user input");
}

void Sprayer::notifyRemoteCommand() {
  if (run_ == Run::Auto) stop("remote command");
}

void Sprayer::remoteStart(int seconds) {
  const long s = constrain(static_cast<long>(seconds), 1L, static_cast<long>(kRemoteMaxS));
  start(Run::Manual, static_cast<unsigned long>(s) * 1000, "remote");
}

void Sprayer::remoteStop() {
  if (pumpOn()) stop("remote");
}

void Sprayer::update() {
  if (!announced_) {
    announced_ = true;
    Serial.println(autoEnabled_ ? "SPRAY auto on at boot: pump runs when auto-aim is on target. "
                                  "Type 'spray off' to disable until reboot."
                                : "SPRAY auto off (type 'spray on' to spray when on target)");
  }

  const unsigned long now = millis();

  if (run_ == Run::Manual && now - startMs_ >= maxMs_) {
    stop("timer");
  } else if (run_ == Run::Auto) {
    if (!aim_.tracking()) {
      stop("target lost or aim paused");
    } else if (now - startMs_ >= maxMs_) {
      stop("max runtime");
      cooling_ = true;
      cooldownUntilMs_ = now + kCooldownMs;
      Serial.printf("SPRAY cooling down %lus\n", kCooldownMs / 1000);
    }
  }

  if (cooling_ && static_cast<long>(now - cooldownUntilMs_) >= 0) cooling_ = false;

  if (run_ == Run::None && autoEnabled_ && !cooling_ && aim_.onTarget()) {
    start(Run::Auto, kMaxSprayMs, "aim on target");
  }

  // Re-assert every pass so the pin always matches the pump state.
  digitalWrite(pin_, pumpOn() ? HIGH : LOW);
}

void Sprayer::printStatus() {
  Serial.printf("SPRAY status auto=%s pump=%s", autoEnabled_ ? "on" : "off",
                run_ == Run::Auto ? "on(auto)" : run_ == Run::Manual ? "on(manual)" : "off");
  if (pumpOn()) {
    Serial.printf(" for %.1fs of %lus", (millis() - startMs_) / 1000.0f, maxMs_ / 1000);
  }
  Serial.printf(" aimOnTarget=%s cooling=%s autoRuns=%lu\n", aim_.onTarget() ? "yes" : "no",
                cooling_ ? "yes" : "no", autoRuns_);
}

bool Sprayer::handleCommand(const char* line) {
  if (strcmp(line, "spray on") == 0) {
    setAuto(true);
  } else if (strcmp(line, "spray off") == 0) {
    setAuto(false);
  } else if (strcmp(line, "spray status") == 0 || strcmp(line, "spray") == 0) {
    printStatus();
  } else if (strcmp(line, "pump off") == 0) {
    if (pumpOn()) {
      stop("command");
    } else {
      Serial.println("SPRAY pump already off");
    }
  } else if (strcmp(line, "pump on") == 0 || strncmp(line, "pump on ", 8) == 0) {
    const long seconds = line[7] ? atol(line + 8) : static_cast<long>(kManualDefaultS);
    if (seconds < 1 || seconds > static_cast<long>(kManualMaxS)) {
      Serial.printf("SPRAY refused: seconds must be 1-%lu\n", kManualMaxS);
      return true;
    }
    start(Run::Manual, static_cast<unsigned long>(seconds) * 1000, "manual");
  } else if (strncmp(line, "spray", 5) == 0 || strncmp(line, "pump", 4) == 0) {
    Serial.printf("Commands: spray on | spray off | spray status | pump on [1-%lu] | pump off\n",
                  kManualMaxS);
  } else {
    return false;
  }
  return true;
}
