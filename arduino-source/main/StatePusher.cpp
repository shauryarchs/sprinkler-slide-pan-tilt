#include "StatePusher.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "AppState.h"
#include "Encoder.h"
#include "LimitSwitch.h"
#include "PanMotor2.h"
#include "SliderMotor1.h"
#include "TiltMotor3.h"

StatePusher::StatePusher(SliderMotor1& slider, PanMotor2& pan, TiltMotor3& tilt,
                         Encoder& enc, LimitSwitch& limit)
    : slider_(slider),
      pan_(pan),
      tilt_(tilt),
      enc_(enc),
      limit_(limit),
      host_(nullptr),
      lastModeName_(""),
      lastMenuIndex_(-1),
      lastLimitEngaged_(false),
      lastHomed_(false),
      lastPushMs_(0) {}

void StatePusher::begin(const char* host) {
  host_ = host;
  xTaskCreatePinnedToCore(taskTrampoline, "state_pusher", kStackBytes, this,
                          kPriority, nullptr, kCore);
}

void StatePusher::taskTrampoline(void* arg) {
  static_cast<StatePusher*>(arg)->run();
}

void StatePusher::run() {
  for (;;) {
    const char* m = modeName(mode);
    const int mi = menuIndex;
    const bool le = limit_.engaged();
    const bool homed = sliderHomed;

    const bool changed = (m != lastModeName_) || (mi != lastMenuIndex_) ||
                         (le != lastLimitEngaged_) || (homed != lastHomed_);

    const unsigned long now = millis();
    const unsigned long sinceLast = now - lastPushMs_;

    if ((changed && sinceLast >= kMinIntervalMs) || sinceLast >= kHeartbeatMs) {
      pushOnce();
      lastModeName_ = m;
      lastMenuIndex_ = mi;
      lastLimitEngaged_ = le;
      lastHomed_ = homed;
      lastPushMs_ = now;
    }

    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

void StatePusher::pushOnce() {
  if (WiFi.status() != WL_CONNECTED) return;

  char body[256];
  snprintf(body, sizeof(body),
           "{\"mode\":\"%s\",\"menuIndex\":%d,\"sliderMm\":%ld,"
           "\"panDeg\":%ld,\"tiltDeg\":%ld,\"dial\":%d,"
           "\"limitEngaged\":%s,\"homed\":%s}",
           modeName(mode), menuIndex, slider_.positionMm(),
           pan_.positionDegrees(), tilt_.positionDegrees(), enc_.position(),
           limit_.engaged() ? "true" : "false",
           sliderHomed ? "true" : "false");

  String url = "https://";
  url += host_;
  url += "/api/motor/state";

  WiFiClientSecure client;
  // Phase 1: server cert is not pinned. Payload has no secrets yet —
  // Phase 2 will introduce a bearer token and proper cert verification.
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(kHttpTimeoutMs);
  if (!https.begin(client, url)) return;
  https.addHeader("Content-Type", "application/json");
  https.POST(body);
  https.end();
}
