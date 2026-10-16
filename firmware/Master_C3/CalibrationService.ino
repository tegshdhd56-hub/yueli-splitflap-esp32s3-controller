#include "CalibrationService.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <errno.h>
#include <stdlib.h>

// Password digest only. Neither the password nor session token is logged or persisted.
static const uint8_t adminPasswordDigest[32] = YUELI_ADMIN_PASSWORD_SHA256_BYTES;
static const uint32_t ADMIN_IDLE_MS = 15UL * 60UL * 1000UL;
static const uint32_t BOOT_WAIT_MS = 20000; // Keep every unit output quiet during power-on self-check.
static const uint16_t BOOT_UNIT_COUNT = ROW_WIDTH * UNITS_AMOUNT;
static const uint8_t CAL_QUEUE_CAPACITY = 8;
static const uint8_t CAL_RX_CAPACITY = 12;
static SemaphoreHandle_t calStateMutex = nullptr;
static SemaphoreHandle_t calTxMutex = nullptr;
static char adminToken[65] = {0};
static uint32_t adminLastActivity = 0;
static uint8_t adminFailedAttempts = 0;
static bool adminLoginThrottled = false;
static uint32_t adminThrottleStarted = 0;
static uint8_t bootHomingState = 0; // 0 waiting, 1 addressed homing, 2 settling, 3 ready
static uint16_t bootHomingCompleted = 0;
static uint32_t bootHomingStarted = 0;
static uint32_t bootSettlingStarted = 0;
static const uint32_t BOOT_SETTLE_MS = 10UL * 60UL * 1000UL;
static uint32_t calNextTransmitAt = 0;
static bool calAwaitingMotionConfirmation = false;
static CalibrationCommand calQueue[CAL_QUEUE_CAPACITY];
static uint8_t calQueueHead = 0;
static uint8_t calQueueSize = 0;
static uint32_t calSequence = 0;
static CalibrationCommand calLastCommand = {};
static bool calLastCommandSent = false;
static uint32_t calLastCommandSentAt = 0;
static CalibrationRxLine calRx[CAL_RX_CAPACITY];
static uint8_t calRxHead = 0;
static uint8_t calRxSize = 0;
static char calRxPartial[128] = {0};
static uint8_t calRxPartialLength = 0;
static uint32_t calRxLastByteAt = 0;
static bool calRxLineTruncated = false;
static bool calRxSawCR = false;
// This is a historical startup report, never a live angle or a save acknowledgement.
struct CalibrationStartupOffset {
  int32_t value;
  uint32_t receivedAt;
  bool valid;
};
static CalibrationStartupOffset calStartupOffsets[256] = {};
static const uint32_t CAL_READ_TIMEOUT_MS = 2000;
enum CalReadState : uint8_t { CAL_READ_NONE, CAL_READ_QUEUED, CAL_READ_WAITING,
  CAL_READ_RECEIVED, CAL_READ_TIMEOUT, CAL_READ_CANCELLED };
struct CalibrationOffsetQuery {
  uint32_t id, commandId, queuedAt, sentAt, completedAt;
  uint8_t dip;
  int8_t page;
  CalReadState state;
  bool hasExpectedOffset, matchesExpected;
  int32_t expectedOffset;
};
struct CalibrationOffsetResult {
  int32_t value;
  uint32_t receivedAt, queryId, commandId;
  bool valid;
};
static CalibrationOffsetQuery calOffsetQuery = {};
static CalibrationOffsetResult calOffsets[256] = {};
static uint32_t calReadSequence = 0;

static bool calReadPendingLocked() {
  return calOffsetQuery.state == CAL_READ_QUEUED || calOffsetQuery.state == CAL_READ_WAITING;
}

static void calQueueReadLocked(uint8_t dip, int8_t page, uint32_t commandId,
                               bool hasExpectedOffset, int32_t expectedOffset, uint32_t now) {
  calOffsetQuery = {};
  if (++calReadSequence == 0) ++calReadSequence;
  calOffsetQuery.id = calReadSequence;
  calOffsetQuery.dip = dip;
  calOffsetQuery.page = page;
  calOffsetQuery.commandId = commandId;
  calOffsetQuery.state = CAL_READ_QUEUED;
  calOffsetQuery.queuedAt = now;
  calOffsetQuery.hasExpectedOffset = hasExpectedOffset;
  calOffsetQuery.expectedOffset = expectedOffset;
}

static void calAppendReadQuery(JsonObject object) {
  static const char* names[] = {"none", "queued", "waiting", "received", "timeout", "cancelled"};
  object["id"] = calOffsetQuery.id;
  object["dip"] = calOffsetQuery.dip;
  object["page"] = calOffsetQuery.page;
  object["commandId"] = calOffsetQuery.commandId;
  object["state"] = names[calOffsetQuery.state];
  object["queuedAt"] = calOffsetQuery.queuedAt;
  object["sentAt"] = calOffsetQuery.sentAt;
  object["completedAt"] = calOffsetQuery.completedAt;
  object["timeoutMs"] = CAL_READ_TIMEOUT_MS;
  if (calOffsetQuery.hasExpectedOffset) object["expectedOffset"] = calOffsetQuery.expectedOffset;
  else object["expectedOffset"] = nullptr;
  if (calOffsetQuery.hasExpectedOffset && calOffsetQuery.state == CAL_READ_RECEIVED)
    object["matchesExpected"] = calOffsetQuery.matchesExpected;
  else object["matchesExpected"] = nullptr;
}

static void calAppendUnitLocked(JsonDocument& doc, uint8_t dip, uint8_t page) {
  doc["dip"] = dip;
  doc["page"] = page;
  doc["readbackSupported"] = false; // No measured/current or per-page angle query exists.
  doc["offsetReadbackSupported"] = true;
  doc["currentAngle"] = nullptr;
  doc["pageAngle"] = nullptr;
  const CalibrationOffsetResult& result = calOffsets[dip];
  if (result.valid) {
    doc["reportedOffset"]["value"] = result.value;
    doc["reportedOffset"]["receivedAt"] = result.receivedAt;
    doc["reportedOffset"]["source"] = "ofget";
    doc["reportedOffset"]["queryId"] = result.queryId;
    doc["reportedOffset"]["commandId"] = result.commandId;
  } else doc["reportedOffset"] = nullptr;
  if (calOffsetQuery.id && calOffsetQuery.dip == dip) calAppendReadQuery(doc["query"].to<JsonObject>());
  else doc["query"] = nullptr;
  doc["message"] = "OFGET returns the unit's global offset in millidegrees, not a page angle or Flash-save acknowledgement.";
}

static void calNormalizePassword(String& password) {
  password.replace("！", "!");
  password.replace("＊", "*");
  password.replace("）", ")");
  password.replace("（", "(");
  password.replace("＠", "@");
  password.replace("＆", "&");
  password.replace("＾", "^");
  // Chinese IMEs turn Shift+6 into the paired ellipsis character sequence.
  password.replace("……", "^");
}

static bool calConstantEquals(const uint8_t* left, const uint8_t* right, size_t length) {
  volatile uint8_t different = 0;
  for (size_t i = 0; i < length; ++i) different |= left[i] ^ right[i];
  return different == 0;
}

static void calEndSessionLocked() {
  if (calReadPendingLocked()) {
    // A cancelled UART query may still answer. Keep a quiet interval before any new TX.
    if (calOffsetQuery.state == CAL_READ_WAITING) {
      uint32_t quietUntil = millis() + CAL_READ_TIMEOUT_MS;
      if (int32_t(quietUntil - calNextTransmitAt) > 0) calNextTransmitAt = quietUntil;
    }
    calOffsetQuery.state = CAL_READ_CANCELLED;
    calOffsetQuery.completedAt = millis();
  }
  memset(adminToken, 0, sizeof(adminToken));
  memset(calQueue, 0, sizeof(calQueue));
  calQueueHead = 0;
  calQueueSize = 0;
  // A command cancelled before transmission must never remain labelled queued/sent.
  if (!calLastCommandSent) memset(&calLastCommand, 0, sizeof(calLastCommand));
}

static void calExpireSessionLocked(uint32_t now) {
  if (adminToken[0] && uint32_t(now - adminLastActivity) >= ADMIN_IDLE_MS) calEndSessionLocked();
  if (adminLoginThrottled && uint32_t(now - adminThrottleStarted) >= 30000) {
    adminLoginThrottled = false;
    adminFailedAttempts = 0;
  }
}

static bool calAuthenticatedLocked(AsyncWebServerRequest* request, uint32_t now) {
  calExpireSessionLocked(now);
  if (!adminToken[0] || !request->hasHeader("Authorization")) return false;
  String supplied = request->getHeader("Authorization")->value();
  if (supplied.length() != 71 || !supplied.startsWith("Bearer ")) return false;
  if (!calConstantEquals(reinterpret_cast<const uint8_t*>(supplied.c_str() + 7),
                         reinterpret_cast<const uint8_t*>(adminToken), 64)) return false;
  return true;
}

static void calJsonResponse(AsyncWebServerRequest* request, int status, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  AsyncWebServerResponse* response = request->beginResponse(status, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}

static void calError(AsyncWebServerRequest* request, int status, const char* error, const char* message) {
  JsonDocument doc;
  doc["error"] = error;
  doc["message"] = message;
  calJsonResponse(request, status, doc);
}

static void calAppendBootLocked(JsonDocument& doc) {
  static const char* names[] = {"waiting", "homing", "settling", "ready"};
  doc["boot"]["state"] = names[bootHomingState];
  doc["boot"]["completed"] = bootHomingCompleted;
  doc["boot"]["total"] = BOOT_UNIT_COUNT;
  doc["calibrationActive"] = adminToken[0] != 0;
  uint32_t now = millis();
  const uint32_t selfCheckElapsed = uint32_t(now - bootHomingStarted);
  doc["boot"]["selfCheckDurationMs"] = BOOT_WAIT_MS;
  doc["boot"]["selfCheckRemainingMs"] = bootHomingState == 0 && selfCheckElapsed < BOOT_WAIT_MS ?
    BOOT_WAIT_MS - selfCheckElapsed : 0;
  uint32_t remaining = int32_t(calNextTransmitAt - now) > 0 ? calNextTransmitAt - now : 0;
  doc["busy"] = bootHomingState != 3 || calAwaitingMotionConfirmation || remaining != 0 || calQueueSize != 0 || calReadPendingLocked();
  doc["retryAfterMs"] = bootHomingState == 3 ? remaining : 0;
  doc["awaitingMotionConfirmation"] = calAwaitingMotionConfirmation;
  doc["boot"]["canConfirm"] = bootHomingState == 2 && uint32_t(now - bootSettlingStarted) >= 3000;
  doc["boot"]["waitRemainingMs"] = bootHomingState == 2 ? remaining : 0;
  doc["boot"]["autoResumeInMs"] = bootHomingState == 2 ? remaining : 0;
  doc["boot"]["retryAfterMs"] = bootHomingState == 2 && uint32_t(now - bootSettlingStarted) < 3000 ?
    3000 - uint32_t(now - bootSettlingStarted) : 0;
}

static bool calIntegerParam(AsyncWebServerRequest* request, const char* name, long minimum, long maximum, long& value, bool post) {
  if (!request->hasParam(name, post)) return false;
  String text = request->getParam(name, post)->value();
  if (text.length() == 0 || text.length() > 8) return false;
  size_t i = text[0] == '-' ? 1 : 0;
  if (i == text.length()) return false;
  long number = 0;
  for (; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + (text[i] - '0');
  }
  if (text[0] == '-') number = -number;
  if (number < minimum || number > maximum) return false;
  value = number;
  return true;
}

static bool calBuildCommand(AsyncWebServerRequest* request, CalibrationCommand& command) {
  if (!request->hasParam("action", true)) return false;
  String action = request->getParam("action", true)->value();
  long dip = 0, value = 0;
  if (!calIntegerParam(request, "dip", 0, 255, dip, true)) return false;
  command.dip = static_cast<uint8_t>(dip);
  command.page = -1;
  if (request->hasParam("page", true)) {
    if (!calIntegerParam(request, "page", 0, 63, value, true)) return false;
    command.page = static_cast<int8_t>(value);
  }
  strlcpy(command.action, action.c_str(), sizeof(command.action));
  command.invalidatesOffset = action == "offset" || action == "clear";
  command.flashWrite = false;
  command.motion = false;
  command.minimumDelayMs = 200;
  if (action == "goto" || action == "learn") {
    if (!calIntegerParam(request, "page", 0, 63, value, true)) return false;
    command.page = static_cast<int8_t>(value);
    command.flashWrite = action == "learn";
    command.motion = action == "goto";
    command.minimumDelayMs = command.motion ? 3000 : 1000;
    snprintf(command.text, sizeof(command.text), "%s %ld %ld", command.flashWrite ? "CMD-LEARN" : "CMD-GOPAGE", dip, value);
  } else if (action == "fine") {
    if (!calIntegerParam(request, "steps", -200, 200, value, true) || value == 0) return false;
    snprintf(command.text, sizeof(command.text), "CMD-FINE %ld %ld", dip, value);
    command.motion = true;
    command.minimumDelayMs = 1000;
  } else if (action == "offset") {
    if (!calIntegerParam(request, "offset", -360000, 360000, value, true)) return false;
    command.flashWrite = true;
    command.minimumDelayMs = 1000;
    command.expectedOffset = static_cast<int32_t>(value);
    snprintf(command.text, sizeof(command.text), "CMD-OFSET %ld %ld", dip, value);
  } else if (action == "clear") {
    command.flashWrite = true;
    command.minimumDelayMs = 1000;
    snprintf(command.text, sizeof(command.text), "CMD-CALCLR %ld", dip);
  } else {
    return false;
  }
  return true;
}

void beginCalibrationService() {
  calStateMutex = xSemaphoreCreateMutex();
  calTxMutex = xSemaphoreCreateRecursiveMutex();
  bootHomingStarted = millis();
  calNextTransmitAt = bootHomingStarted + BOOT_WAIT_MS;
  inputText = "";
  lastWrittenText = "";
}

bool calibrationDisplayLocked() {
  if (!calStateMutex || !calTxMutex) return true;
  xSemaphoreTake(calStateMutex, portMAX_DELAY);
  calExpireSessionLocked(millis());
  bool locked = bootHomingState != 3 || adminToken[0] != 0 || calAwaitingMotionConfirmation || calReadPendingLocked() ||
    int32_t(calNextTransmitAt - millis()) > 0;
  xSemaphoreGive(calStateMutex);
  return locked;
}

bool beginDisplayUpdate() {
  if (!calStateMutex || !calTxMutex) return false;
  xSemaphoreTakeRecursive(calTxMutex, portMAX_DELAY);
  if (calibrationDisplayLocked()) {
    xSemaphoreGiveRecursive(calTxMutex);
    return false;
  }
  return true;
}

void endDisplayUpdate() {
  xSemaphoreGiveRecursive(calTxMutex);
}

// Caller owns the UART mutex. No interrupt-disabled critical section covers UART flush.
static void calTransmitLine(const char* command) {
  digitalWrite(RS485_EN_PIN, HIGH);
  Serial1.write(reinterpret_cast<const uint8_t*>(command), strlen(command));
  Serial1.write('\n');
  Serial1.flush();
  digitalWrite(RS485_EN_PIN, LOW);
}

bool sendDisplayRS485(const String& command) {
  if (!beginDisplayUpdate()) return false;
  calTransmitLine(command.c_str());
  endDisplayUpdate();
  return true;
}

void sendDisplayLockedResponse(AsyncWebServerRequest* request) {
  calError(request, 423, "display_locked", "Display paused for startup homing, administrator calibration, or unconfirmed motion.");
}

static int calHexDigit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

static void calRecordStartupOffsetLocked(const char* line, uint32_t now) {
  // Exact known firmware grammar: "[<decimal DIP>] (0x<two hex DIP>)ofset: <signed value>".
  // Both address representations must agree. Reject oversized, partial and malformed lines.
  if (line[0] != '[' || line[1] < '0' || line[1] > '9') return;
  char* end = nullptr;
  errno = 0;
  unsigned long dip = strtoul(line + 1, &end, 10);
  if (errno || dip > 255 || strncmp(end, "] (0x", 5) != 0) return;
  const char* hex = end + 5;
  if (strlen(hex) < 11) return;
  int high = calHexDigit(hex[0]), low = calHexDigit(hex[1]);
  if (high < 0 || low < 0 || unsigned(high * 16 + low) != dip || strncmp(hex + 2, ")ofset: ", 8) != 0) return;
  const char* valueStart = hex + 10;
  const char* digits = valueStart[0] == '-' ? valueStart + 1 : valueStart;
  if (*digits < '0' || *digits > '9') return;
  errno = 0;
  long value = strtol(valueStart, &end, 10);
  if (errno || *end != 0 || value < -360000 || value > 360000) return;
  calStartupOffsets[dip].value = static_cast<int32_t>(value);
  calStartupOffsets[dip].receivedAt = now;
  calStartupOffsets[dip].valid = true;
}

static void calRecordOffsetReplyLocked(const char* line, uint32_t now) {
  // Only accept a complete, strictly parsed reply to the one outstanding query.
  // The wire protocol has no transaction ID: local IDs correlate UI requests only.
  if (calOffsetQuery.state != CAL_READ_WAITING ||
      uint32_t(now - calOffsetQuery.sentAt) >= CAL_READ_TIMEOUT_MS ||
      strncmp(line, "OFGET ", 6) != 0 || line[6] < '0' || line[6] > '9') return;
  char* end = nullptr;
  errno = 0;
  unsigned long dip = strtoul(line + 6, &end, 10);
  if (errno || dip > 255 || dip != calOffsetQuery.dip || *end != ' ') return;
  const char* valueStart = end + 1;
  const char* digits = (*valueStart == '-' || *valueStart == '+') ? valueStart + 1 : valueStart;
  if (*digits < '0' || *digits > '9') return;
  errno = 0;
  long value = strtol(valueStart, &end, 10);
  if (errno || *end || value < -360000 || value > 360000) return;
  CalibrationOffsetResult& result = calOffsets[dip];
  result.value = static_cast<int32_t>(value);
  result.receivedAt = now;
  result.queryId = calOffsetQuery.id;
  result.commandId = calOffsetQuery.commandId;
  result.valid = true;
  calOffsetQuery.state = CAL_READ_RECEIVED;
  calOffsetQuery.completedAt = now;
  calOffsetQuery.matchesExpected = calOffsetQuery.hasExpectedOffset && value == calOffsetQuery.expectedOffset;
  calNextTransmitAt = now + 100; // Preserve a frame boundary before any subsequent command.
}

static void calAppendRxLocked(uint32_t now, bool completeLine) {
  if (!calRxPartialLength) return;
  calRxPartial[calRxPartialLength] = 0;
  if (completeLine && !calRxLineTruncated) {
    calRecordStartupOffsetLocked(calRxPartial, now);
    calRecordOffsetReplyLocked(calRxPartial, now);
  }
  uint8_t slot = (calRxHead + calRxSize) % CAL_RX_CAPACITY;
  calRx[slot].at = now;
  memcpy(calRx[slot].text, calRxPartial, calRxPartialLength + 1);
  if (calRxSize < CAL_RX_CAPACITY) ++calRxSize;
  else calRxHead = (calRxHead + 1) % CAL_RX_CAPACITY;
  calRxPartialLength = 0;
}

static void calCaptureRxLocked(uint32_t now) {
  unsigned int budget = 256;
  while (budget-- && Serial1.available()) {
    char ch = char(Serial1.read());
    calRxLastByteAt = now;
    if (ch == '\n') {
      calAppendRxLocked(now, true);
      calRxLineTruncated = false;
      calRxSawCR = false;
      continue;
    }
    if (ch == '\r') {
      if (calRxSawCR) calRxLineTruncated = true;
      calRxSawCR = true;
      continue;
    }
    // CR may only appear immediately before LF, never inside a numeric field.
    if (calRxSawCR) { calRxLineTruncated = true; calRxSawCR = false; }
    if (calRxPartialLength == sizeof(calRxPartial) - 1) {
      calAppendRxLocked(now, false);
      calRxLineTruncated = true;
    }
    // Keep RX diagnostic output printable, bounded and safe for JSON/textContent.
    calRxPartial[calRxPartialLength++] = (ch >= 32 && ch <= 126) ? ch : '.';
  }
  if (calRxPartialLength && uint32_t(now - calRxLastByteAt) >= 100) {
    calAppendRxLocked(now, false);
    calRxLineTruncated = true;
  }
}

void serviceCalibration() {
  if (!calStateMutex || !calTxMutex) return;
  xSemaphoreTakeRecursive(calTxMutex, portMAX_DELAY);
  xSemaphoreTake(calStateMutex, portMAX_DELAY);
  uint32_t now = millis();
  calExpireSessionLocked(now);
  // Keep expired deadlines fresh while running so signed wrap-safe comparisons
  // cannot reinterpret a months-old deadline as a future cooldown.
  if (bootHomingState == 3 && int32_t(now - calNextTransmitAt) >= 0) calNextTransmitAt = now;
  calCaptureRxLocked(now);
  if (calOffsetQuery.state == CAL_READ_WAITING && uint32_t(now - calOffsetQuery.sentAt) >= CAL_READ_TIMEOUT_MS) {
    calOffsetQuery.state = CAL_READ_TIMEOUT;
    calOffsetQuery.completedAt = now;
    // Quarantine late replies before a retry; RX continues draining while timed out.
    // No finite guard can disambiguate arbitrarily late same-DIP replies without a wire ID.
    calNextTransmitAt = now + CAL_READ_TIMEOUT_MS;
  }
  if (bootHomingState == 0 && uint32_t(now - bootHomingStarted) >= BOOT_WAIT_MS) bootHomingState = 1;
  if (int32_t(now - calNextTransmitAt) >= 0) {
    if (bootHomingState == 1) {
      if (bootHomingCompleted < BOOT_UNIT_COUNT) {
        char command[32];
        snprintf(command, sizeof(command), "CMD-GOPAGE %u 0", bootHomingCompleted);
        calTransmitLine(command);
        ++bootHomingCompleted;
        calNextTransmitAt = millis() + 100;
      } else {
        calTransmitLine("CMD-POS 0");
        bootHomingState = 2;
        bootSettlingStarted = millis();
        calNextTransmitAt = bootSettlingStarted + BOOT_SETTLE_MS;
      }
    } else if (bootHomingState == 2) {
      bootHomingState = 3;
      inputText = "";
      lastWrittenText = "";
    } else if (bootHomingState == 3 && adminToken[0] && !calAwaitingMotionConfirmation && calQueueSize) {
      CalibrationCommand command = calQueue[calQueueHead];
      calQueueHead = (calQueueHead + 1) % CAL_QUEUE_CAPACITY;
      --calQueueSize;
      calTransmitLine(command.text);
      // RX buffered before this write may have refreshed an older startup report.
      if (command.invalidatesOffset) calStartupOffsets[command.dip].valid = false;
      if (calLastCommand.id == command.id) {
        calLastCommandSent = true;
        calLastCommandSentAt = millis();
      }
      calAwaitingMotionConfirmation = command.motion;
      calNextTransmitAt = millis() + command.minimumDelayMs;
    } else if (bootHomingState == 3 && adminToken[0] && !calAwaitingMotionConfirmation &&
               !calQueueSize && calOffsetQuery.state == CAL_READ_QUEUED &&
               !Serial1.available() && !calRxPartialLength && uint32_t(now - calRxLastByteAt) >= 20) {
      // Send OFGET only after movement was observed stopped or the Flash-write guard
      // elapsed. The supplied unit has a single RX buffer, not a reliable command FIFO.
      char command[32];
      calRxSawCR = false;
      calRxLineTruncated = false;
      snprintf(command, sizeof(command), "CMD-OFGET %u", calOffsetQuery.dip);
      calTransmitLine(command);
      calOffsetQuery.sentAt = millis();
      calOffsetQuery.state = CAL_READ_WAITING;
      calNextTransmitAt = calOffsetQuery.sentAt + CAL_READ_TIMEOUT_MS;
    }
  }
  xSemaphoreGive(calStateMutex);
  xSemaphoreGiveRecursive(calTxMutex);
}

void registerCalibrationRoutes(AsyncWebServer& server) {
  server.on("/device/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    calExpireSessionLocked(millis());
    calAppendBootLocked(doc);
    xSemaphoreGive(calStateMutex);
    calJsonResponse(request, 200, doc);
  });

  server.on("/device/confirm-homed", HTTP_POST, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    int status = 200;
    xSemaphoreTakeRecursive(calTxMutex, portMAX_DELAY);
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    uint32_t now = millis();
    if (bootHomingState == 2 && uint32_t(now - bootSettlingStarted) >= 3000) {
      bootHomingState = 3;
      calNextTransmitAt = now;
      inputText = "";
      lastWrittenText = "";
      doc["message"] = "Startup wait ended by user observation. No hardware acknowledgement was received.";
    } else if (bootHomingState != 3) {
      status = 409;
      doc["error"] = "startup_homing";
      doc["message"] = "Wait until all homing commands have been sent and at least 3 seconds have elapsed.";
    }
    calAppendBootLocked(doc);
    xSemaphoreGive(calStateMutex);
    xSemaphoreGiveRecursive(calTxMutex);
    calJsonResponse(request, status, doc);
  });

  server.on("/admin/login", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("password", true)) {
      calError(request, 400, "missing_password", "Administrator password is required.");
      return;
    }
    String supplied = request->getParam("password", true)->value();
    if (supplied.length() > 128) {
      calError(request, 400, "invalid_password", "Invalid administrator password.");
      return;
    }
    uint8_t digest[32] = {0};
    calNormalizePassword(supplied);
    mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(supplied.c_str()), supplied.length(), digest, 0);
    const bool matches = calConstantEquals(digest, adminPasswordDigest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
    supplied = "";
    JsonDocument doc;
    int status = 200;
    xSemaphoreTakeRecursive(calTxMutex, portMAX_DELAY);
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    uint32_t now = millis();
    calExpireSessionLocked(now);
    if (adminLoginThrottled && uint32_t(now - adminThrottleStarted) >= 30000) {
      adminLoginThrottled = false;
      adminFailedAttempts = 0;
    }
    if (adminLoginThrottled) {
      status = 429;
      doc["error"] = "login_rate_limited";
      doc["retryAfter"] = (30000 - uint32_t(now - adminThrottleStarted) + 999) / 1000;
      doc["message"] = "Too many failed attempts. Try again shortly.";
    } else if (!matches) {
      status = 401;
      doc["error"] = "incorrect_password";
      doc["message"] = "Incorrect administrator password.";
      if (++adminFailedAttempts >= 5) {
        adminLoginThrottled = true;
        adminThrottleStarted = now;
      }
    } else {
      calEndSessionLocked();
      uint8_t random[32];
      esp_fill_random(random, sizeof(random));
      static const char hex[] = "0123456789abcdef";
      for (size_t i = 0; i < sizeof(random); ++i) {
        adminToken[i * 2] = hex[random[i] >> 4];
        adminToken[i * 2 + 1] = hex[random[i] & 15];
      }
      adminToken[64] = 0;
      memset(random, 0, sizeof(random));
      adminLastActivity = now;
      adminFailedAttempts = 0;
      doc["token"] = adminToken;
      doc["expiresIn"] = ADMIN_IDLE_MS / 1000;
      doc["message"] = "Calibration unlocked. Normal display and automatic modes are paused.";
      calAppendBootLocked(doc);
    }
    xSemaphoreGive(calStateMutex);
    xSemaphoreGiveRecursive(calTxMutex);
    calJsonResponse(request, status, doc);
  });

  server.on("/admin/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    uint32_t now = millis();
    if (!calAuthenticatedLocked(request, now)) {
      xSemaphoreGive(calStateMutex);
      calError(request, 401, "authentication_required", "Sign in to administrator calibration.");
      return;
    }
    // Polling is not human activity: it must not keep an unattended session alive.
    doc["authenticated"] = true;
    doc["expiresIn"] = (ADMIN_IDLE_MS - uint32_t(now - adminLastActivity) + 999) / 1000;
    calAppendBootLocked(doc);
    if (calLastCommand.id) {
      doc["lastCommand"]["id"] = calLastCommand.id;
      doc["lastCommand"]["command"] = calLastCommand.text;
      doc["lastCommand"]["state"] = calLastCommandSent ? "sent" : "queued";
      doc["lastCommand"]["sentAt"] = calLastCommandSentAt;
      doc["lastCommand"]["dip"] = calLastCommand.dip;
      doc["lastCommand"]["action"] = calLastCommand.action;
      if (calLastCommand.page >= 0) doc["lastCommand"]["page"] = calLastCommand.page;
      else doc["lastCommand"]["page"] = nullptr;
    } else doc["lastCommand"] = nullptr;
    doc["queued"] = calQueueSize;
    if (calOffsetQuery.id) calAppendReadQuery(doc["unitQuery"].to<JsonObject>());
    else doc["unitQuery"] = nullptr;
    JsonArray rx = doc["rx"].to<JsonArray>();
    for (uint8_t i = 0; i < calRxSize; ++i) {
      const CalibrationRxLine& line = calRx[(calRxHead + i) % CAL_RX_CAPACITY];
      JsonObject item = rx.add<JsonObject>();
      item["at"] = line.at;
      item["text"] = line.text;
    }
    xSemaphoreGive(calStateMutex);
    calJsonResponse(request, 200, doc);
  });

  server.on("/admin/unit", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    long dip = 0, page = 0;
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    if (!calAuthenticatedLocked(request, millis())) {
      xSemaphoreGive(calStateMutex);
      calError(request, 401, "authentication_required", "Sign in to developer calibration.");
      return;
    }
    if (!calIntegerParam(request, "dip", 0, 255, dip, false) ||
        !calIntegerParam(request, "page", 0, 63, page, false)) {
      xSemaphoreGive(calStateMutex);
      calError(request, 400, "invalid_unit_parameters", "DIP must be 0..255 and page must be 0..63.");
      return;
    }
    calAppendUnitLocked(doc, dip, page);
    xSemaphoreGive(calStateMutex);
    calJsonResponse(request, 200, doc);
  });

  server.on("/admin/unit/read", HTTP_POST, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    long dip = 0, page = 0;
    int status = 202;
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    uint32_t now = millis();
    if (!calAuthenticatedLocked(request, now)) {
      status = 401; doc["error"] = "authentication_required";
    } else if (!calIntegerParam(request, "dip", 0, 255, dip, true) ||
               !calIntegerParam(request, "page", 0, 63, page, true)) {
      status = 400; doc["error"] = "invalid_unit_parameters";
    } else if (bootHomingState != 3) {
      status = 409; doc["error"] = "startup_homing";
    } else if (calReadPendingLocked()) {
      if (calOffsetQuery.dip != dip || calOffsetQuery.page != page) {
        status = 409; doc["error"] = "readback_busy";
      } else calAppendUnitLocked(doc, dip, page); // Idempotent while this query is outstanding.
    } else if (calAwaitingMotionConfirmation || calQueueSize || int32_t(calNextTransmitAt - now) > 0) {
      status = 409; doc["error"] = "motion_wait";
    } else {
      calQueueReadLocked(dip, page, 0, false, 0, now);
      adminLastActivity = now;
      calAppendUnitLocked(doc, dip, page);
    }
    xSemaphoreGive(calStateMutex);
    calJsonResponse(request, status, doc);
  });

  server.on("/admin/calibrate", HTTP_POST, [](AsyncWebServerRequest* request) {
    CalibrationCommand command = {};
    const bool confirmStopped = request->hasParam("action", true) &&
      request->getParam("action", true)->value() == "confirm_stopped";
    const bool valid = calBuildCommand(request, command);
    JsonDocument doc;
    int status = 202;
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    uint32_t now = millis();
    if (!calAuthenticatedLocked(request, now)) {
      status = 401;
      doc["error"] = "authentication_required";
    } else if (bootHomingState != 3) {
      status = 409;
      doc["error"] = "startup_homing";
      doc["message"] = "Wait until startup homing is complete.";
    } else if (confirmStopped) {
      if (int32_t(calNextTransmitAt - now) > 0 || calQueueSize) {
        status = 409;
        doc["error"] = "motion_wait";
        doc["message"] = "Wait for the minimum motion interval before confirming the unit has stopped.";
      } else {
        calAwaitingMotionConfirmation = false;
        adminLastActivity = now;
        status = 200;
        doc["message"] = "Motion confirmed by administrator observation; no hardware acknowledgement was received.";
      }
    } else if (!valid) {
      status = 400;
      doc["error"] = "invalid_calibration_parameters";
    } else if (calAwaitingMotionConfirmation || calQueueSize || int32_t(calNextTransmitAt - now) > 0 || calReadPendingLocked()) {
      status = 409;
      doc["error"] = "motion_wait";
      doc["message"] = "Wait for transmission/cooldown and confirm the unit has stopped before the next command.";
    } else if (calQueueSize >= CAL_QUEUE_CAPACITY) {
      status = 429;
      doc["error"] = "calibration_queue_full";
    } else {
      command.id = ++calSequence;
      if (command.invalidatesOffset) {
        calStartupOffsets[command.dip].valid = false;
        calOffsets[command.dip].valid = false;
      }
      calQueue[(calQueueHead + calQueueSize) % CAL_QUEUE_CAPACITY] = command;
      ++calQueueSize;
      calLastCommand = command;
      calLastCommandSent = false;
      calLastCommandSentAt = 0;
      adminLastActivity = now;
      calQueueReadLocked(command.dip, command.page, command.id,
                         strcmp(command.action, "offset") == 0, command.expectedOffset, now);
      doc["id"] = command.id;
      doc["command"] = command.text;
      doc["state"] = "queued";
      doc["dip"] = command.dip;
      doc["action"] = command.action;
      if (command.page >= 0) doc["page"] = command.page;
      else doc["page"] = nullptr;
      doc["message"] = "Queued for transmission; this firmware does not provide calibration acknowledgements.";
    }
    calAppendBootLocked(doc);
    xSemaphoreGive(calStateMutex);
    calJsonResponse(request, status, doc);
  });

  server.on("/admin/logout", HTTP_POST, [](AsyncWebServerRequest* request) {
    xSemaphoreTakeRecursive(calTxMutex, portMAX_DELAY);
    xSemaphoreTake(calStateMutex, portMAX_DELAY);
    if (!calAuthenticatedLocked(request, millis())) {
      xSemaphoreGive(calStateMutex);
      xSemaphoreGiveRecursive(calTxMutex);
      calError(request, 401, "authentication_required", "Sign in to administrator calibration.");
      return;
    }
    calEndSessionLocked();
    JsonDocument doc;
    calAppendBootLocked(doc);
    xSemaphoreGive(calStateMutex);
    xSemaphoreGiveRecursive(calTxMutex);
    doc["authenticated"] = false;
    doc["message"] = "Calibration locked and queued commands cancelled. Normal display resumes after startup and any pending motion confirmation.";
    calJsonResponse(request, 200, doc);
  });
}
