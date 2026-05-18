#include "CommandPoller.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <string.h>
#include <stdlib.h>

CommandPoller::CommandPoller() : host_(nullptr), queue_(nullptr) {}

void CommandPoller::begin(const char* host) {
  host_ = host;
  queue_ = xQueueCreate(kQueueDepth, sizeof(RemoteCommand));
  xTaskCreatePinnedToCore(taskTrampoline, "cmd_poller", kStackBytes, this,
                          kPriority, nullptr, kCore);
}

void CommandPoller::taskTrampoline(void* arg) {
  static_cast<CommandPoller*>(arg)->run();
}

void CommandPoller::run() {
  for (;;) {
    pollOnce();
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

void CommandPoller::pollOnce() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Skip when the main loop hasn't drained yet — keeps memory bounded
  // if we ever fall behind dispatching.
  if (uxQueueSpacesAvailable(queue_) == 0) return;

  String url = "https://";
  url += host_;
  url += "/api/motor/command?since=";
  url += String(lastSeqAck);

  WiFiClientSecure client;
  // Same Phase 2 caveat as StatePusher: cert isn't pinned. The /api/
  // motor/command endpoints are open, so there are no secrets in flight
  // here — only the request shape leaks.
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(kHttpTimeoutMs);
  if (!https.begin(client, url)) return;
  const int code = https.GET();
  if (code != 200) {
    https.end();
    return;
  }
  String body = https.getString();
  https.end();

  RemoteCommand cmd = {};
  if (parseCommandJson(body.c_str(), &cmd)) {
    xQueueSend(queue_, &cmd, 0);
  }
}

// --- Tiny JSON field extraction. ---
//
// The /api/motor/command payload is fully under our control and has a
// fixed shape, so a targeted strstr-based extractor is cheaper than
// pulling in ArduinoJson. Caveats: doesn't handle escaped quotes,
// doesn't handle whitespace inside the matched key, doesn't tolerate a
// key as a substring of another key. None of those apply to our schema.

namespace {

bool extractStringField(const char* json, const char* key, char* out,
                        size_t outLen) {
  char pattern[32];
  // "<key>":"
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* p = strstr(json, pattern);
  if (!p) return false;
  p += strlen(pattern);
  const char* end = strchr(p, '"');
  if (!end) return false;
  const size_t len = static_cast<size_t>(end - p);
  if (len >= outLen) return false;
  memcpy(out, p, len);
  out[len] = '\0';
  return true;
}

bool extractLongField(const char* json, const char* key, long* out) {
  char pattern[32];
  // "<key>":
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* p = strstr(json, pattern);
  if (!p) return false;
  p += strlen(pattern);
  while (*p == ' ') p++;
  char* endp = nullptr;
  const long val = strtol(p, &endp, 10);
  if (endp == p) return false;
  *out = val;
  return true;
}

}  // namespace

bool CommandPoller::parseCommandJson(const char* json, RemoteCommand* out) {
  if (!json) return false;
  // Empty-object response means "no command newer than ?since" — skip
  // silently. Also covers the case where the worker has never had a
  // command written to it.
  if (strstr(json, "\"kind\"") == nullptr) return false;

  long seq = 0;
  if (!extractLongField(json, "seq", &seq) || seq <= 0) return false;

  char kindStr[24];
  if (!extractStringField(json, "kind", kindStr, sizeof(kindStr))) return false;

  out->seq = static_cast<unsigned long>(seq);

  if (strcmp(kindStr, "enterMode") == 0) {
    out->kind = RemoteCmdKind::EnterMode;
    char modeStr[24];
    if (!extractStringField(json, "mode", modeStr, sizeof(modeStr))) return false;
    if (strcmp(modeStr, "menu") == 0) out->targetMode = Mode::Menu;
    else if (strcmp(modeStr, "motor1") == 0) out->targetMode = Mode::Motor1Control;
    else if (strcmp(modeStr, "motor2") == 0) out->targetMode = Mode::Motor2Control;
    else if (strcmp(modeStr, "motor3") == 0) out->targetMode = Mode::Motor3Control;
    else if (strcmp(modeStr, "allmotors") == 0) out->targetMode = Mode::AllMotorsControl;
    else return false;
    return true;
  }
  if (strcmp(kindStr, "rehome") == 0)         { out->kind = RemoteCmdKind::Rehome;         return true; }
  if (strcmp(kindStr, "zeroPan") == 0)        { out->kind = RemoteCmdKind::ZeroPan;        return true; }
  if (strcmp(kindStr, "zeroTilt") == 0)       { out->kind = RemoteCmdKind::ZeroTilt;       return true; }
  if (strcmp(kindStr, "setSliderSpeed") == 0) {
    out->kind = RemoteCmdKind::SetSliderSpeed;
    long speed = 0;
    if (!extractLongField(json, "speed", &speed)) return false;
    out->speed = static_cast<int>(speed);
    return true;
  }
  if (strcmp(kindStr, "nudgePan") == 0 || strcmp(kindStr, "nudgeTilt") == 0) {
    out->kind = (strcmp(kindStr, "nudgePan") == 0)
                    ? RemoteCmdKind::NudgePan
                    : RemoteCmdKind::NudgeTilt;
    long deltaDeg = 0;
    if (!extractLongField(json, "deltaDeg", &deltaDeg)) return false;
    out->deltaDeg = static_cast<int>(deltaDeg);
    return true;
  }
  return false;
}
