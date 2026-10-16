#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include "Constants.h"
#include "WifiProvisioning.h"

extern const char* wifiPath;
extern bool wifiConnected;

static portMUX_TYPE provisionMux = portMUX_INITIALIZER_UNLOCKED;
static ProvisionStatus provisionStatus = {"idle", "", "", "Choose a nearby Wi-Fi network.", false, "", ""};
static ProvisionScan provisionScan = {"idle", 0, {}};
static ProvisionRequest provisionPending = {};
static bool provisionConnectQueued = false;
static bool provisionScanQueued = false;
static bool provisionBusy = false;
static uint32_t provisionIpEvents = 0;
static uint8_t provisionDisconnectReason = 0;
static char provisionDisconnectSsid[33] = {};
static bool provisionScanReserved = false; // Includes queued, starting, running and cancellation drain.
static bool provisionScanCallbackExpected = false;
static uint32_t provisionScanEvents = 0;
static ProvisionScan provisionScanCompletion = {};
static uint32_t provisionScanCompletedAt = 0;

// The following state is accessed only by setup()/loop().
static ProvisionRequest provisionSaved = {};
static ProvisionRequest provisionAttempt = {};
static bool provisionAttemptActive = false;
static bool provisionAttemptStarted = false;
static bool provisionAttemptIsUser = false;
enum ProvisionScanPhase : uint8_t { SCAN_IDLE, SCAN_STARTING, SCAN_RUNNING, SCAN_DRAINING };
static ProvisionScanPhase provisionScanPhase = SCAN_IDLE;
static const uint32_t PROVISION_SCAN_TIMEOUT_MS = 18000;
static const uint32_t PROVISION_SCAN_RETRY_MS = 500;
static uint32_t provisionScanEventBaseline = 0;
static uint32_t provisionScanNextStartAt = 0;
static uint32_t provisionScanDrainAt = 0;
static uint32_t provisionPhaseAt = 0;
static uint32_t provisionScanAt = 0;
static uint32_t provisionIpBaseline = 0;
static uint32_t provisionRetryAt = 0;
static bool provisionHaveTerminalResult = false;

void setupMDNS(const char* hostname) {
  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started: http://%s.local\n", hostname);
  } else {
    Serial.println("Error setting up mDNS responder!");
  }
}

static void publishProvisionResult(const char* state, const char* message, const char* ip) {
  portENTER_CRITICAL(&provisionMux);
  strlcpy(provisionStatus.state, state, sizeof(provisionStatus.state));
  strlcpy(provisionStatus.ssid, provisionAttempt.ssid, sizeof(provisionStatus.ssid));
  strlcpy(provisionStatus.ip, ip, sizeof(provisionStatus.ip));
  strlcpy(provisionStatus.message, message, sizeof(provisionStatus.message));
  strlcpy(provisionStatus.clientRequestId, provisionAttempt.clientRequestId, sizeof(provisionStatus.clientRequestId));
  portEXIT_CRITICAL(&provisionMux);
}

// Arduino core 2.0.10 has already consumed the driver's AP records in _scanDone()
// before invoking onEvent. This event task is the ONLY owner of its result cache:
// copy fixed-size records and delete it here, never race scanDelete() from loop().
// No Strings, allocation, flash access, driver calls or Serial logging are used.
static void captureProvisionScan(const wifi_event_sta_scan_done_t& event) {
  static ProvisionScan completed; // Avoid placing ~2 KB on the event task stack.
  memset(&completed, 0, sizeof(completed));
  strlcpy(completed.state, "ready", sizeof(completed.state));
  completed.completionStatus = event.status;
  completed.scanId = event.scan_id;
  completed.scannedCount = event.number;
  if (event.status != 0) {
    strlcpy(completed.state, "error", sizeof(completed.state));
    strlcpy(completed.errorCode, "SCAN_EVENT_FAILED", sizeof(completed.errorCode));
    strlcpy(completed.message, "The Wi-Fi driver could not complete the scan. Select Refresh to try again.", sizeof(completed.message));
  } else {
    for (uint16_t i = 0; i < event.number; ++i) {
      const wifi_ap_record_t* record = static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
      if (!record) {
        completed.count = 0;
        strlcpy(completed.state, "error", sizeof(completed.state));
        strlcpy(completed.errorCode, "SCAN_RESULTS_UNAVAILABLE", sizeof(completed.errorCode));
        strlcpy(completed.message, "Scan results could not be read. Select Refresh to try again.", sizeof(completed.message));
        break;
      }
      char ssid[33];
      memcpy(ssid, record->ssid, 32);
      ssid[32] = 0;
      if (!ssid[0]) continue;
      int target = -1;
      for (uint8_t j = 0; j < completed.count; ++j) {
        if (!strcmp(ssid, completed.networks[j].ssid)) { target = j; break; }
      }
      if (target >= 0) {
        if (record->rssi <= completed.networks[target].rssi) continue;
      } else if (completed.count < WIFI_NETWORK_LIMIT) {
        target = completed.count++;
      } else {
        target = 0;
        for (uint8_t j = 1; j < completed.count; ++j)
          if (completed.networks[j].rssi < completed.networks[target].rssi) target = j;
        if (record->rssi <= completed.networks[target].rssi) continue;
      }
      ProvisionNetwork& item = completed.networks[target];
      strlcpy(item.ssid, ssid, sizeof(item.ssid));
      item.rssi = record->rssi;
      item.secure = record->authmode != WIFI_AUTH_OPEN;
    }
    for (uint8_t i = 0; i < completed.count; ++i) {
      for (uint8_t j = i + 1; j < completed.count; ++j) {
        if (completed.networks[j].rssi > completed.networks[i].rssi) {
          ProvisionNetwork swap = completed.networks[i];
          completed.networks[i] = completed.networks[j];
          completed.networks[j] = swap;
        }
      }
    }
    if (!completed.message[0]) strlcpy(completed.message, completed.count ?
      "Nearby Wi-Fi networks are ready." : "No visible 2.4 GHz networks found. Move closer or enter a hidden network name.", sizeof(completed.message));
  }
  WiFi.scanDelete(); // Cache-only operation; core has finished writing it on this same task.
  const uint32_t completedAt = millis();
  portENTER_CRITICAL(&provisionMux);
  if (provisionScanCallbackExpected) {
    provisionScanCompletion = completed;
    provisionScanCompletedAt = completedAt;
  }
  ++provisionScanEvents;
  portEXIT_CRITICAL(&provisionMux);
}

// Only SCAN_DONE reads/deletes the already-built Arduino cache as explained above.
// All other events just publish fixed-size state; connection/driver work is in loop().
static void provisionEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
    captureProvisionScan(info.wifi_scan_done);
    return;
  }
  portENTER_CRITICAL(&provisionMux);
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    ++provisionIpEvents;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // Ignore our deliberate disconnects (including the core's one automatic retry).
    if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) {
      provisionDisconnectReason = info.wifi_sta_disconnected.reason;
      size_t n = info.wifi_sta_disconnected.ssid_len;
      if (n > 32) n = 32;
      memcpy(provisionDisconnectSsid, info.wifi_sta_disconnected.ssid, n);
      provisionDisconnectSsid[n] = 0;
    }
  }
  portEXIT_CRITICAL(&provisionMux);
}

static bool persistProvisionCredentials() {
  // Commit only after a verified connection. Failed writes retain the old file.
  const char* temporary = "/wifi.pending";
  File f = LittleFS.open(temporary, "w");
  if (!f) return false;
  String data = String(provisionAttempt.ssid) + "\n" + provisionAttempt.password;
  size_t written = f.print(data);
  f.flush();
  f.close();
  if (written != data.length()) {
    LittleFS.remove(temporary);
    return false;
  }
  if (!LittleFS.rename(temporary, wifiPath)) {
    LittleFS.remove(temporary);
    return false;
  }
  provisionSaved = provisionAttempt;
  return true;
}

static void startProvisionAttempt(const ProvisionRequest& credentials, bool fromUser) {
  provisionAttempt = credentials;
  provisionAttemptIsUser = fromUser;
  provisionAttemptActive = true;
  provisionAttemptStarted = false;
  provisionPhaseAt = millis();
  portENTER_CRITICAL(&provisionMux);
  provisionBusy = true;
  provisionDisconnectReason = 0;
  provisionIpBaseline = provisionIpEvents;
  portEXIT_CRITICAL(&provisionMux);
  // Disconnect only STA. The YUELI access point stays enabled for recovery.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  if (fromUser || !provisionHaveTerminalResult) {
    publishProvisionResult("connecting", "Connecting to Wi-Fi...", "");
  }
}

static void endProvisionAttempt(const char* state, const char* message) {
  if (provisionAttemptIsUser || !provisionHaveTerminalResult) {
    publishProvisionResult(state, message, "");
    if (provisionAttemptIsUser) provisionHaveTerminalResult = true;
  }
  provisionAttemptActive = false;
  provisionAttemptStarted = false;
  WiFi.disconnect(false, false);
  memset(provisionAttempt.password, 0, sizeof(provisionAttempt.password));
  provisionRetryAt = millis();
  portENTER_CRITICAL(&provisionMux);
  provisionBusy = false;
  portEXIT_CRITICAL(&provisionMux);
}

void initWiFi() {
  WiFi.persistent(false); // Unverified passwords must never enter NVS.
  WiFi.setAutoReconnect(false); // loop() coordinates retries with scans.
  WiFi.onEvent(provisionEvent);
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(MASTER_AP_SSID, MASTER_AP_PASS, 1, 0, 8)) {
    Serial.print("[WiFi] YUELI access point: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[WiFi] Access point setup failed");
  }
  File f = LittleFS.open(wifiPath, "r");
  if (f) {
    String ssid = f.readStringUntil('\n');
    String password = f.readStringUntil('\n');
    f.close();
    // Remove only CR from line endings. Spaces can be valid credentials.
    if (ssid.endsWith("\r")) ssid.remove(ssid.length() - 1);
    if (password.endsWith("\r")) password.remove(password.length() - 1);
    if (ssid.length() > 0 && ssid.length() <= 32 && password.length() <= 64) {
      strlcpy(provisionSaved.ssid, ssid.c_str(), sizeof(provisionSaved.ssid));
      strlcpy(provisionSaved.password, password.c_str(), sizeof(provisionSaved.password));
      startProvisionAttempt(provisionSaved, false);
    }
  }
}

static void sendProvisionJson(AsyncWebServerRequest* request, int code, JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void handleProvisionScan(AsyncWebServerRequest* request) {
  bool refresh = request->method() == HTTP_POST || request->hasParam("refresh");
  ProvisionScan snapshot;
  bool busy;
  portENTER_CRITICAL(&provisionMux);
  busy = provisionBusy;
  if (!provisionScanReserved &&
      (refresh || !strcmp(provisionScan.state, "idle"))) {
    provisionScanQueued = true;
    provisionScanReserved = true;
    ++provisionScan.requestId;
    if (!provisionScan.requestId) ++provisionScan.requestId;
    strlcpy(provisionScan.state, "scanning", sizeof(provisionScan.state));
    provisionScan.count = 0;
    provisionScan.message[0] = 0;
    provisionScan.errorCode[0] = 0;
    provisionScan.driverError = 0;
    provisionScan.completionStatus = -1;
    provisionScan.elapsedMs = 0;
    provisionScan.startedAt = millis();
    provisionScan.scannedCount = 0;
    provisionScan.scanId = 0;
    provisionScan.cleanupPending = false;
  }
  snapshot = provisionScan;
  portEXIT_CRITICAL(&provisionMux);
  JsonDocument doc;
  doc["scanEngine"] = "r7-event";
  doc["state"] = busy ? "busy" : snapshot.state;
  if (busy) doc["message"] = "A Wi-Fi connection is in progress. Try scanning again shortly.";
  else if (snapshot.message[0]) doc["message"] = snapshot.message;
  doc["errorCode"] = snapshot.errorCode;
  doc["driverError"] = snapshot.driverError;
  doc["completionStatus"] = snapshot.completionStatus;
  doc["elapsedMs"] = !strcmp(snapshot.state, "scanning") ? uint32_t(millis() - snapshot.startedAt) : snapshot.elapsedMs;
  doc["requestId"] = snapshot.requestId;
  doc["scanId"] = snapshot.scanId;
  doc["scannedCount"] = snapshot.scannedCount;
  doc["cleanupPending"] = snapshot.cleanupPending;
  doc["timeoutMs"] = PROVISION_SCAN_TIMEOUT_MS;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (uint8_t i = 0; i < snapshot.count; ++i) {
    JsonObject item = networks.add<JsonObject>();
    item["ssid"] = snapshot.networks[i].ssid;
    item["rssi"] = snapshot.networks[i].rssi;
    item["secure"] = snapshot.networks[i].secure;
  }
  sendProvisionJson(request, 200, doc);
}

void registerWiFiRoutes(AsyncWebServer& server) {
  // Register before serveStatic: credentials must never be served as assets.
  server.on("/wifi.txt", HTTP_ANY, [](AsyncWebServerRequest* request) { request->send(404); });
  server.on("/wifi.pending", HTTP_ANY, [](AsyncWebServerRequest* request) { request->send(404); });
  server.on("/wifi/scan", HTTP_GET, handleProvisionScan);
  server.on("/wifi/scan", HTTP_POST, handleProvisionScan);
  server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    ProvisionStatus snapshot;
    portENTER_CRITICAL(&provisionMux);
    snapshot = provisionStatus;
    portEXIT_CRITICAL(&provisionMux);
    JsonDocument doc;
    doc["state"] = snapshot.state;
    doc["ssid"] = snapshot.ssid;
    doc["ip"] = snapshot.ip;
    doc["message"] = snapshot.message;
    doc["connected"] = snapshot.connected;
    doc["currentSsid"] = snapshot.currentSsid;
    doc["currentIp"] = snapshot.currentIp;
    doc["clientRequestId"] = snapshot.clientRequestId;
    sendProvisionJson(request, 200, doc);
  });
  server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
      doc["state"] = "failed";
      doc["message"] = "Choose a Wi-Fi network and enter its password.";
      sendProvisionJson(request, 400, doc);
      return;
    }
    String ssid = request->getParam("ssid", true)->value();
    String password = request->getParam("password", true)->value();
    String requestId = request->hasParam("clientRequestId", true) ? request->getParam("clientRequestId", true)->value() : "";
    bool validId = requestId.length() <= 64;
    for (size_t i = 0; i < requestId.length(); ++i) {
      if (requestId[i] < 33 || requestId[i] > 126) validId = false;
    }
    bool valid = ssid.length() > 0 && ssid.length() <= 32 && password.length() <= 64;
    if (password.length() > 0 && password.length() < 8) valid = false;
    if (password.length() == 64) {
      for (size_t i = 0; i < password.length(); ++i) {
        if (!isxdigit(static_cast<unsigned char>(password[i]))) valid = false;
      }
    }
    valid = valid && ssid.indexOf('\n') < 0 && ssid.indexOf('\r') < 0 &&
      password.indexOf('\n') < 0 && password.indexOf('\r') < 0;
    valid = valid && strlen(ssid.c_str()) == ssid.length() && strlen(password.c_str()) == password.length();
    if (!valid || !validId) {
      doc["state"] = "failed";
      doc["message"] = "Use a Wi-Fi name of 1-32 bytes and an 8-63 byte password (or 64 hex digits). Open networks use an empty password.";
      sendProvisionJson(request, 400, doc);
      return;
    }
    bool busy;
    portENTER_CRITICAL(&provisionMux);
    busy = provisionBusy || provisionScanReserved;
    if (!busy) {
      strlcpy(provisionPending.ssid, ssid.c_str(), sizeof(provisionPending.ssid));
      strlcpy(provisionPending.password, password.c_str(), sizeof(provisionPending.password));
      strlcpy(provisionPending.clientRequestId, requestId.c_str(), sizeof(provisionPending.clientRequestId));
      provisionConnectQueued = true;
      provisionBusy = true;
      strlcpy(provisionStatus.state, "connecting", sizeof(provisionStatus.state));
      strlcpy(provisionStatus.ssid, ssid.c_str(), sizeof(provisionStatus.ssid));
      strlcpy(provisionStatus.clientRequestId, requestId.c_str(), sizeof(provisionStatus.clientRequestId));
      provisionStatus.ip[0] = 0;
      strlcpy(provisionStatus.message, "Connecting to Wi-Fi...", sizeof(provisionStatus.message));
    }
    portEXIT_CRITICAL(&provisionMux);
    doc["state"] = busy ? "busy" : "connecting";
    if (busy) doc["message"] = "Please wait for the current scan or connection to finish.";
    else {
      doc["ssid"] = ssid;
      doc["clientRequestId"] = requestId;
    }
    sendProvisionJson(request, busy ? 409 : 202, doc);
  });
}

static void failProvisionScan(const char* code, const char* message, esp_err_t driverError, bool draining) {
  uint32_t elapsed = millis() - provisionScanAt;
  portENTER_CRITICAL(&provisionMux);
  strlcpy(provisionScan.state, "error", sizeof(provisionScan.state));
  strlcpy(provisionScan.errorCode, code, sizeof(provisionScan.errorCode));
  strlcpy(provisionScan.message, message, sizeof(provisionScan.message));
  provisionScan.count = 0;
  provisionScan.driverError = driverError;
  provisionScan.elapsedMs = elapsed;
  provisionScan.cleanupPending = draining;
  provisionScanReserved = draining;
  if (!draining) provisionScanCallbackExpected = false;
  portEXIT_CRITICAL(&provisionMux);
  Serial.printf("[WiFi scan] error=%s driver=%s(%d) elapsed=%lu ms cleanup=%u\n",
    code, esp_err_to_name(driverError), int(driverError), static_cast<unsigned long>(elapsed), unsigned(draining));
}

static void beginProvisionScan() {
  provisionScanPhase = SCAN_STARTING;
  provisionScanAt = millis();
  provisionScanNextStartAt = provisionScanAt;
  portENTER_CRITICAL(&provisionMux);
  provisionScan.startedAt = provisionScanAt;
  strlcpy(provisionScan.message, "Scanning nearby 2.4 GHz Wi-Fi networks...", sizeof(provisionScan.message));
  portEXIT_CRITICAL(&provisionMux);
}

static void serviceProvisionScan() {
  uint32_t now = millis();
  if (provisionScanPhase == SCAN_STARTING) {
    if (uint32_t(now - provisionScanAt) >= PROVISION_SCAN_TIMEOUT_MS) {
      provisionScanPhase = SCAN_IDLE;
      failProvisionScan("SCAN_START_TIMEOUT", "Wi-Fi is still busy. Wait a moment, then select Refresh.", ESP_ERR_WIFI_STATE, false);
      return;
    }
    if (int32_t(now - provisionScanNextStartAt) < 0) return;
    wifi_scan_config_t config = {}; // Every field, including passive time, must be initialized.
    config.channel = 0;
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 120;
    config.scan_time.active.max = 300;
    config.scan_time.passive = 360;
    // Reserve before driver start: even a very fast completion must be captured.
    portENTER_CRITICAL(&provisionMux);
    provisionScanEventBaseline = provisionScanEvents;
    provisionScanCallbackExpected = true;
    portEXIT_CRITICAL(&provisionMux);
    esp_err_t error = esp_wifi_scan_start(&config, false);
    if (error == ESP_OK) {
      provisionScanPhase = SCAN_RUNNING;
      portENTER_CRITICAL(&provisionMux);
      provisionScan.driverError = ESP_OK;
      strlcpy(provisionScan.message, "Scanning nearby 2.4 GHz Wi-Fi networks...", sizeof(provisionScan.message));
      portEXIT_CRITICAL(&provisionMux);
      Serial.printf("[WiFi scan] start engine=r7-event active=120/300 passive=360 timeout=%lu ms connected=%u\n",
        static_cast<unsigned long>(PROVISION_SCAN_TIMEOUT_MS), unsigned(WiFi.status() == WL_CONNECTED));
    } else {
      portENTER_CRITICAL(&provisionMux);
      provisionScanCallbackExpected = false;
      provisionScan.driverError = error;
      portEXIT_CRITICAL(&provisionMux);
      if (error == ESP_ERR_WIFI_STATE) {
        // The core may still be finishing its one automatic connection retry.
        // Wait in place without disconnecting STA/AP or starting another connection.
        provisionScanNextStartAt = millis() + PROVISION_SCAN_RETRY_MS;
        portENTER_CRITICAL(&provisionMux);
        strlcpy(provisionScan.message, "Wi-Fi is busy. Waiting to scan...", sizeof(provisionScan.message));
        portEXIT_CRITICAL(&provisionMux);
        Serial.printf("[WiFi scan] start deferred driver=%s(%d) elapsed=%lu ms\n",
          esp_err_to_name(error), int(error), static_cast<unsigned long>(millis() - provisionScanAt));
      } else {
        provisionScanPhase = SCAN_IDLE;
        failProvisionScan(esp_err_to_name(error), "The Wi-Fi driver could not start scanning. Select Refresh to try again.", error, false);
      }
      return;
    }
  }
  if (provisionScanPhase != SCAN_RUNNING && provisionScanPhase != SCAN_DRAINING) return;

  ProvisionScan completed;
  uint32_t completedAt = 0;
  bool eventReady;
  portENTER_CRITICAL(&provisionMux);
  eventReady = provisionScanEvents != provisionScanEventBaseline;
  if (eventReady) {
    completed = provisionScanCompletion;
    completedAt = provisionScanCompletedAt;
    provisionScanCallbackExpected = false;
  }
  portEXIT_CRITICAL(&provisionMux);
  if (eventReady) {
    if (provisionScanPhase == SCAN_DRAINING) {
      // A cancellation event belongs only to the timed-out request. Never publish
      // its partial results or start another request before its cache is deleted.
      portENTER_CRITICAL(&provisionMux);
      provisionScanReserved = false;
      provisionScan.cleanupPending = false;
      provisionScan.completionStatus = completed.completionStatus;
      provisionScan.scanId = completed.scanId;
      strlcpy(provisionScan.errorCode, "SCAN_TIMEOUT", sizeof(provisionScan.errorCode));
      strlcpy(provisionScan.message, "The scan timed out. Select Refresh to try again.", sizeof(provisionScan.message));
      portEXIT_CRITICAL(&provisionMux);
      Serial.printf("[WiFi scan] cancelled scanId=%u status=%ld drain=%lu ms\n", unsigned(completed.scanId),
        static_cast<long>(completed.completionStatus), static_cast<unsigned long>(millis() - provisionScanDrainAt));
      provisionScanPhase = SCAN_IDLE;
      return;
    }
    completed.elapsedMs = completedAt - provisionScanAt;
    if (completed.elapsedMs > PROVISION_SCAN_TIMEOUT_MS) {
      provisionScanPhase = SCAN_IDLE;
      failProvisionScan("SCAN_TIMEOUT", "The scan timed out. Select Refresh to try again.", ESP_ERR_TIMEOUT, false);
      return;
    }
    portENTER_CRITICAL(&provisionMux);
    completed.startedAt = provisionScan.startedAt;
    completed.requestId = provisionScan.requestId;
    provisionScan = completed;
    provisionScanReserved = false;
    portEXIT_CRITICAL(&provisionMux);
    provisionScanPhase = SCAN_IDLE;
    Serial.printf("[WiFi scan] done state=%s scanId=%u status=%ld found=%u shown=%u elapsed=%lu ms error=%s\n",
      completed.state, unsigned(completed.scanId), static_cast<long>(completed.completionStatus),
      unsigned(completed.scannedCount), unsigned(completed.count), static_cast<unsigned long>(completed.elapsedMs), completed.errorCode);
    return;
  }
  now = millis();
  if (provisionScanPhase == SCAN_RUNNING && uint32_t(now - provisionScanAt) >= PROVISION_SCAN_TIMEOUT_MS) {
    // esp_wifi_scan_stop produces SCAN_DONE. Keep reservation until the callback
    // has copied/deleted that cache. If the event is ever lost, fail closed rather
    // than misattribute its delayed results to a fresh scan or race a connection.
    provisionScanPhase = SCAN_DRAINING;
    provisionScanDrainAt = now;
    esp_err_t stopError = esp_wifi_scan_stop();
    failProvisionScan("SCAN_TIMEOUT", "The scan timed out. Waiting for scan cleanup before retrying.", ESP_ERR_TIMEOUT, true);
    Serial.printf("[WiFi scan] stop driver=%s(%d)\n", esp_err_to_name(stopError), int(stopError));
  } else if (provisionScanPhase == SCAN_DRAINING && uint32_t(now - provisionScanDrainAt) >= 2000) {
    portENTER_CRITICAL(&provisionMux);
    strlcpy(provisionScan.errorCode, "SCAN_CLEANUP_PENDING", sizeof(provisionScan.errorCode));
    strlcpy(provisionScan.message, "The Wi-Fi driver has not finished cancelling the scan. Wait and refresh; if this persists, restart the device.", sizeof(provisionScan.message));
    portEXIT_CRITICAL(&provisionMux);
  }
}

void serviceWiFi() {
  uint32_t now = millis();
  ProvisionRequest queued = {};
  bool connectQueued, scanQueued;
  portENTER_CRITICAL(&provisionMux);
  connectQueued = provisionConnectQueued;
  // A refresh requested while connecting waits; it must not reuse stale results.
  scanQueued = provisionScanQueued && !provisionAttemptActive && !connectQueued && provisionScanPhase == SCAN_IDLE;
  if (connectQueued) {
    queued = provisionPending;
    memset(&provisionPending, 0, sizeof(provisionPending));
    provisionConnectQueued = false;
  }
  if (scanQueued) provisionScanQueued = false;
  portEXIT_CRITICAL(&provisionMux);
  if (connectQueued) startProvisionAttempt(queued, true);
  memset(queued.password, 0, sizeof(queued.password));
  if (scanQueued) beginProvisionScan();
  serviceProvisionScan();
  now = millis();
  if (provisionAttemptActive && !provisionAttemptStarted && now - provisionPhaseAt >= 300) {
    wifi_ap_record_t previousAp;
    // Confirm STA is actually detached, including same-SSID password changes.
    if (esp_wifi_sta_get_ap_info(&previousAp) == ESP_OK) {
      if (now - provisionPhaseAt >= 3000) {
        endProvisionAttempt("failed", "Could not switch Wi-Fi networks. Please try again.");
      }
      return;
    }
    // Drain the previous station's disconnect event before beginning.
    portENTER_CRITICAL(&provisionMux);
    provisionDisconnectReason = 0;
    provisionIpBaseline = provisionIpEvents;
    portEXIT_CRITICAL(&provisionMux);
    provisionAttemptStarted = true;
    provisionPhaseAt = now;
    WiFi.begin(provisionAttempt.ssid, provisionAttempt.password);
  }
  if (provisionAttemptActive && provisionAttemptStarted) {
    uint8_t reason;
    uint32_t ipEvents;
    char eventSsid[33];
    portENTER_CRITICAL(&provisionMux);
    reason = provisionDisconnectReason;
    ipEvents = provisionIpEvents;
    strlcpy(eventSsid, provisionDisconnectSsid, sizeof(eventSsid));
    provisionDisconnectReason = 0;
    portEXIT_CRITICAL(&provisionMux);
    wifi_ap_record_t associatedAp;
    bool verified = esp_wifi_sta_get_ap_info(&associatedAp) == ESP_OK &&
      ipEvents != provisionIpBaseline && WiFi.status() == WL_CONNECTED &&
      WiFi.SSID() == provisionAttempt.ssid && uint32_t(WiFi.localIP()) != 0;
    if (verified) {
      bool saved = !provisionAttemptIsUser || persistProvisionCredentials();
      if (provisionAttemptIsUser || !provisionHaveTerminalResult) {
        publishProvisionResult("connected", saved ? "Connected successfully." :
          "Connected, but the Wi-Fi settings could not be saved. Please try again.", WiFi.localIP().toString().c_str());
      }
      if (provisionAttemptIsUser) provisionHaveTerminalResult = false;
      provisionAttemptActive = false;
      provisionAttemptStarted = false;
      memset(provisionAttempt.password, 0, sizeof(provisionAttempt.password));
      portENTER_CRITICAL(&provisionMux);
      provisionBusy = false;
      portEXIT_CRITICAL(&provisionMux);
    } else if (reason && (!eventSsid[0] || !strcmp(eventSsid, provisionAttempt.ssid))) {
      if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
          reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_802_1X_AUTH_FAILED) {
        endProvisionAttempt("wrong_password", "Incorrect password or authentication rejected. Check the password and try again.");
      } else if (reason == WIFI_REASON_NO_AP_FOUND) {
        endProvisionAttempt("not_found", "Network not found. Use a nearby 2.4 GHz Wi-Fi network.");
      } else if (reason == WIFI_REASON_ASSOC_FAIL) {
        endProvisionAttempt("failed", "The router rejected the connection. Please try again.");
      }
    }
    if (provisionAttemptActive && now - provisionPhaseAt >= 30000) {
      endProvisionAttempt("failed", "Connection timed out. Check the network signal and router, then try again.");
    }
  }
  bool connected = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (connected && !wifiConnected) {
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());
  }
  wifiConnected = connected;
  String currentSsid = connected ? WiFi.SSID() : "";
  String currentIp = connected ? WiFi.localIP().toString() : "";
  now = millis();
  portENTER_CRITICAL(&provisionMux);
  provisionStatus.connected = connected;
  strlcpy(provisionStatus.currentSsid, currentSsid.c_str(), sizeof(provisionStatus.currentSsid));
  strlcpy(provisionStatus.currentIp, currentIp.c_str(), sizeof(provisionStatus.currentIp));
  // DHCP may renew an address after the original provisioning attempt ended.
  if (connected && !strcmp(provisionStatus.state, "connected")) {
    strlcpy(provisionStatus.ip, currentIp.c_str(), sizeof(provisionStatus.ip));
  }
  if (!connected && !provisionBusy && !strcmp(provisionStatus.state, "connected")) {
    strlcpy(provisionStatus.state, "idle", sizeof(provisionStatus.state));
    provisionStatus.ip[0] = 0;
    strlcpy(provisionStatus.message, "Wi-Fi disconnected. Waiting to reconnect.", sizeof(provisionStatus.message));
  }
  bool retry = !connected && !provisionAttemptActive && !provisionScanReserved &&
    !provisionBusy && !provisionScanQueued && provisionSaved.ssid[0] && now - provisionRetryAt >= 20000;
  if (retry) provisionBusy = true; // Reserve before callbacks can enqueue new work.
  portEXIT_CRITICAL(&provisionMux);
  // Restore verified credentials, but keep a user's failure result visible.
  if (retry) {
    startProvisionAttempt(provisionSaved, false);
  }
}
