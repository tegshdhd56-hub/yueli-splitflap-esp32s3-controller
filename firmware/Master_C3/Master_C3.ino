// YUELI Split-Flap controller firmware — https://www.yueli.hk/
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <ezTime.h>
#include <LittleFS.h>
#include "Classes.h"
#include "Constants.h"
#include "WifiProvisioning.h"
#include "CalibrationService.h"
#include "CarouselService.h"
#include "HistoryBody.h"

// Definitions for extern constants
const char* ALIGNMENT_MODE_DEFAULT = "right";
const char* DEVICE_MODE_TEXT = "text";
const char* scheduledMessagesPath = "/scheduled-messages.txt";
const char* DEVICE_MODE_COUNTDOWN = "countdown";
const char* DEVICE_MODE_CLOCK = "clock";
const char* DEVICE_MODE_DATE = "date";
const char* DEVICE_MODE_CAROUSEL = "carousel";

// Configuration
// SERIAL_ENABLE defined in Constants.h
// SERIAL_BAUDRATE defined in Constants.h
// UNITS_AMOUNT defined in Constants.h
// MASTER_AP_SSID defined in Constants.h
// MASTER_AP_PASS defined in Constants.h
// TCP_PORT defined in Constants.h

// Global Objects
AsyncWebServer webServer(80);
AsyncServer tcpServer(TCP_PORT);
Timezone timezone;

// TCP Clients (Slaves)
// Mapping: row ID (0-3) -> Client*
AsyncClient* slaveClients[UNITS_AMOUNT] = {NULL, NULL, NULL, NULL, NULL, NULL};
bool slaveConnected[UNITS_AMOUNT] = {false, false, false, false, false, false};

// Shared Variables
String alignment = "center";
String flapSpeed = "1";
String deviceMode = "text";
String inputText = "";
String lastWrittenText = "";
bool alignmentUpdated = false;
bool wifiConnected = false;
String countdownToDateUnix = "0";
LList<ScheduledMessage> scheduledMessages;

// Carousel Variables
LList<String> carouselMessages;
long carouselInterval = 5000; // Legacy storage units: 1000 per minute; default 5 minutes
int carouselIndex = 0;
unsigned long lastCarouselChange = 0;

// Constants (copied from original)
const char* PARAM_ALIGNMENT = "alignment";
const char* PARAM_FLAP_SPEED = "flapSpeed";
const char* PARAM_DEVICEMODE = "deviceMode";
const char* PARAM_INPUT_TEXT = "inputText";
const char* PARAM_SCHEDULE_ENABLED = "scheduleEnabled";
const char* PARAM_SCHEDULE_DATE_TIME = "scheduledDateTimeUnix";
const char* PARAM_SCHEDULE_SHOW_INDEFINITELY = "scheduleShowIndefinitely";
const char* PARAM_COUNTDOWN_DATE = "countdownDateTimeUnix";
const char* PARAM_ID = "id";

// definitions moved to top

// Files for persistence
const char* alignmentPath = "/alignment.txt";
const char* flapSpeedPath = "/flapspeed.txt";
const char* deviceModePath = "/devicemode.txt";
const char* countdownPath = "/countdown.txt";
const char* wifiPath = "/wifi.txt";
const char* carouselPath = "/carousel.json";
// scheduledMessagesPath defined at top

extern const char* ALIGNMENT_MODE_DEFAULT;
extern const char* DEVICE_MODE_TEXT;
extern const char* scheduledMessagesPath;
extern const char* wifiPath;
extern void writeFile(fs::FS &fs, const char * path, const char * message);
extern String readWholeFile(fs::FS &fs, const char * path, String defaultValue);
extern void setupMDNS(const char* hostname);
void checkScheduledMessages();
void checkCountdown();
void showText(String msg); 
void showText(String msg, int delayMs);
String getCurrentSettingValues();
bool removeScheduledMessage(long id);
void addAndPersistScheduledMessage(String msg, long time, bool indef);

// Carousel Helpers
void loadCarousel();
void checkCarousel();

static void sendUncached(AsyncWebServerRequest* request, int status, const char* type, const String& content) {
  auto* response = request->beginResponse(status, type, content);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void servePublicAsset(AsyncWebServerRequest* request, const char* path, const char* type) {
  auto* response = request->beginResponse(LittleFS, path, type);
  if (!response) {
    sendUncached(request, 404, "text/plain; charset=utf-8", "Not Found");
    return;
  }
  // Only this release's exact versioned assets may be reused without a request.
  // The entry page and unversioned/old URLs always fetch the current bytes.
  const bool currentAsset = strcmp(path, "/index.html") != 0 &&
    request->hasParam("v") && request->getParam("v")->value() == YUELI_ASSET_VERSION;
  response->addHeader("Cache-Control", currentAsset ? "public, max-age=31536000, immutable" : "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}

static void registerPublicAssets(AsyncWebServer& server) {
  // Exact routes prevent accidental exposure of settings, credentials or backups.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/index.html", "text/html; charset=utf-8"); });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/index.html", "text/html; charset=utf-8"); });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/script.js", "application/javascript; charset=utf-8"); });
  server.on("/wifi.js", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/wifi.js", "application/javascript; charset=utf-8"); });
  server.on("/admin.js", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/admin.js", "application/javascript; charset=utf-8"); });
  server.on("/carousel.js", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/carousel.js", "application/javascript; charset=utf-8"); });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/style.css", "text/css; charset=utf-8"); });
  server.on("/carousel.css", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/carousel.css", "text/css; charset=utf-8"); });
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/favicon.png", "image/png"); });
  server.on("/yueli-logo.png", HTTP_GET, [](AsyncWebServerRequest* request) { servePublicAsset(request, "/yueli-logo.png", "image/png"); });
}

static void registerHistoryRoutes(AsyncWebServer& server) {
  const char* historyPath = "/history.json";

  server.on("/history", HTTP_GET, [historyPath](AsyncWebServerRequest * request) {
       String content = readWholeFile(LittleFS, historyPath, "[]");
       sendUncached(request, 200, "application/json; charset=utf-8", content);
  });

  server.on("/history", HTTP_POST, [historyPath](AsyncWebServerRequest * request) {
      auto* body = static_cast<HistoryBody*>(request->_tempObject);
      if (request->contentLength() > HISTORY_BODY_LIMIT) {
        sendUncached(request, 413, "application/json", "{\"error\":\"history_too_large\"}");
        return;
      }
      if (!body) {
        sendUncached(request, request->contentLength() ? 503 : 400, "application/json", "{\"error\":\"history_body_unavailable\"}");
        return;
      }
      if (body->invalid || body->received != body->total) {
        sendUncached(request, 400, "application/json", "{\"error\":\"incomplete_history\"}");
        return;
      }
      if (!beginDisplayUpdate()) {
        sendDisplayLockedResponse(request);
        return;
      }
      const char* temporary = "/history.tmp";
      File file = LittleFS.open(temporary, "w");
      bool saved = file && file.write(reinterpret_cast<const uint8_t*>(body->text), body->received) == body->received;
      file.flush();
      file.close();
      if (saved) saved = LittleFS.rename(temporary, historyPath);
      if (!saved) LittleFS.remove(temporary);
      endDisplayUpdate();
      sendUncached(request, saved ? 200 : 507, "application/json", saved ? "{\"status\":\"ok\"}" : "{\"error\":\"history_storage_failed\"}");
  }, nullptr, [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0 && !request->_tempObject) request->_tempObject = createHistoryBody(total);
      appendHistoryBody(static_cast<HistoryBody*>(request->_tempObject), data, len, index, total);
      // ESPAsyncWebServer frees this request-owned malloc block on completion/disconnect.
  });
}

// RS485 Configuration
#define RS485_EN_PIN 21
void rs485Send_speed(String data) {
String cmdText = "CMD-SPEED " + data;
  if (sendDisplayRS485(cmdText)) Serial.println("[RS485] Sent SPEED: " + cmdText);
}
void rs485Send_str(String data) {
String dataText = data;
dataText.replace("\\n", "");
        dataText.replace("\n", "");
        dataText.replace("\r", "");
        dataText.replace("●","a");
        dataText.replace("◆","b");
        dataText.replace("★","c");
        dataText.replace("▲","d");
        dataText.replace("■","e");
        dataText.replace("▼","f");
        dataText.replace("○","g");

        // 检查dataText的第一个字符是否为空格，如果是则替换为^
        if (dataText.length() > 0 && dataText.charAt(0) == ' ') {
          dataText.setCharAt(0, '^');
        }
        String cmdText = "CMD-STR " + dataText;
  if (sendDisplayRS485(cmdText)) Serial.println("[RS485] Sent: " + cmdText);
}

// --- TCP Server Logic ---

void handleNewClient(void* arg, AsyncClient* client) {
  Serial.printf("[TCP] New client: %s\n", client->remoteIP().toString().c_str());

  // Simple identification by IP last octet
  // 101 -> Row 0, 102 -> Row 1, ...
  int lastOctet = client->remoteIP()[3];
  int rowId = lastOctet - 101;

  if (rowId >= 0 && rowId < UNITS_AMOUNT) {
    slaveClients[rowId] = client;
    slaveConnected[rowId] = true;
    Serial.printf("[TCP] Registered Row %d\n", rowId);
    
    client->onData([](void* arg, AsyncClient* c, void *data, size_t len) {
       // Handle ACK or Status
       Serial.write((uint8_t*)data, len);
    });

    client->onDisconnect([rowId](void* arg, AsyncClient* c) {
      Serial.printf("[TCP] Row %d Disconnected\n", rowId);
      slaveConnected[rowId] = false;
      slaveClients[rowId] = NULL;
    });
  } else {
    Serial.println("[TCP] Unknown Client IP");
    client->close();
  }
}

void setup() {
  beginCalibrationService(); // Start the one-time self-check clock before any setup work.
  Serial.begin(SERIAL_BAUDRATE);
  Serial1.setRxBufferSize(8192); // Retain staggered unit startup reports while Wi-Fi initializes.
  Serial1.begin(SERIAL1_BAUDRATE, SERIAL_8N1, 18, 17);
  pinMode(RS485_EN_PIN, OUTPUT);
  digitalWrite(RS485_EN_PIN, LOW);
  beginCarouselService();
  Serial.println("\n\n--- Split Flap Master (ESP32-S3) Starting ---");
  Serial.println("[YUELI] Official website: " YUELI_WEBSITE_URL);
  Serial.println("[Serial1] UART1 initialized at " + String(SERIAL1_BAUDRATE) + " baud (RX:IO18, TX:IO17)");
  Serial.println("[RS485] EN Pin: GPIO" + String(RS485_EN_PIN));

  initialiseFileSystem();
  loadValuesFromFileSystem();
  loadCarousel();

  initWiFi();
  registerWiFiRoutes(webServer);
  registerCalibrationRoutes(webServer);

  registerPublicAssets(webServer);

  webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
      String json = getCurrentSettingValues();
      sendUncached(request, 200, "application/json; charset=utf-8", json);
  });
  
  webServer.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
      JsonDocument doc;
      doc["device"]["product"] = YUELI_PRODUCT_NAME;
      doc["device"]["firmware"] = YUELI_FIRMWARE_VERSION;
      doc["device"]["website"] = YUELI_WEBSITE_URL;
      doc["wifi"]["connected"] = (WiFi.status() == WL_CONNECTED);
      doc["wifi"]["ip"] = WiFi.localIP().toString();
      doc["wifi"]["ssid"] = WiFi.SSID();

      String json;
      serializeJson(doc, json);
      sendUncached(request, 200, "application/json; charset=utf-8", json);
  });


  webServer.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      if (!beginDisplayUpdate()) {
        sendDisplayLockedResponse(request);
        return;
      }
      const String requestedMode = request->hasParam(PARAM_DEVICEMODE, true) ?
        request->getParam(PARAM_DEVICEMODE, true)->value() : deviceMode;
      if (requestedMode != DEVICE_MODE_CAROUSEL && request->hasParam(PARAM_INPUT_TEXT, true)) {
        inputText = request->getParam(PARAM_INPUT_TEXT, true)->value();
        
        rs485Send_str(inputText);
         
      }
      if (request->hasParam(PARAM_DEVICEMODE, true)) {
        String mode = request->getParam(PARAM_DEVICEMODE, true)->value();
        if (deviceMode != mode) {
           deviceMode = mode;
           Serial.println("[DeviceMode] Set to: " + mode);
           writeFile(LittleFS, deviceModePath, deviceMode.c_str());
        }
        if (mode == DEVICE_MODE_CAROUSEL) restartCarouselPlayback();
      }
      if (request->hasParam(PARAM_ALIGNMENT, true)) {
         String align = request->getParam(PARAM_ALIGNMENT, true)->value();
         if (alignment != align) {
            alignment = align;
            alignmentUpdated = true;
            writeFile(LittleFS, alignmentPath, alignment.c_str());
         }
      }
      if (request->hasParam(PARAM_FLAP_SPEED, true)) {
         String speed = request->getParam(PARAM_FLAP_SPEED, true)->value();
         if (flapSpeed != speed) {
            flapSpeed = speed;
            Serial.println("[FlapSpeed] Set to: " + speed);
            
            rs485Send_speed(speed);
            writeFile(LittleFS, flapSpeedPath, flapSpeed.c_str());
         }
      }

      if (request->hasParam(PARAM_COUNTDOWN_DATE, true)) {
         String dateUnix = request->getParam(PARAM_COUNTDOWN_DATE, true)->value();
         if (dateUnix.length() > 0 && countdownToDateUnix != dateUnix) {
            countdownToDateUnix = dateUnix;
            writeFile(LittleFS, countdownPath, countdownToDateUnix.c_str());
         }
      }

      if (request->hasParam(PARAM_SCHEDULE_ENABLED, true)) {
         String dateParam = "0";
         if (request->hasParam(PARAM_SCHEDULE_DATE_TIME, true)) {
            dateParam = request->getParam(PARAM_SCHEDULE_DATE_TIME, true)->value();
         }
         
         bool showIndefinitely = false;
         if (request->hasParam(PARAM_SCHEDULE_SHOW_INDEFINITELY, true)) {
            showIndefinitely = true;
         }

         long scheduledTime = atol(dateParam.c_str());
         if (scheduledTime > 0) {
            String msg = inputText;
            if (request->hasParam(PARAM_INPUT_TEXT, true)) {
               msg = request->getParam(PARAM_INPUT_TEXT, true)->value();
            }
            addAndPersistScheduledMessage(msg, scheduledTime, showIndefinitely);
         }
      }

      endDisplayUpdate();
      request->redirect("/");
  });

  webServer.on("/scheduled-message/remove", HTTP_DELETE, [](AsyncWebServerRequest * request) {
      if (request->hasParam(PARAM_ID)) {
          long id = request->getParam(PARAM_ID)->value().toInt();
          bool removed = removeScheduledMessage(id);
          if (removed) {
             request->send(202, "text/plain", "Accepted");
          } else {
             request->send(404, "text/plain", "Not Found");
          }
      } else {
        request->send(400, "text/plain", "Bad Request");
      }
  });

  setupMDNS("split-flap");

  registerHistoryRoutes(webServer);

  registerCarouselRoutes(webServer);

  webServer.begin();

  unsigned long startTime = millis();
  unsigned long timeout = 5000; 
  while (timeStatus() != timeSet && millis() - startTime < timeout) {
    delay(100);
  }
  
  if (timeStatus() == timeSet) {
    Serial.println("[Time] NTP synchronized successfully");
  } else {
    Serial.println("[Time] NTP sync timed out - continuing in AP mode");
  }
  
  timezone.setLocation("Asia/Shanghai");
}
void loop() {
  serviceWiFi();
  serviceCalibration();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 1000) {
    lastCheck = millis();
    if (!beginDisplayUpdate()) return;
    checkScheduledMessages();
    checkCountdown();
    checkCarousel();

    if (deviceMode == DEVICE_MODE_DATE) {
       rs485Send_str(timezone.dateTime("Y.m.d"));
    } 
    else if (deviceMode == DEVICE_MODE_CLOCK) {
       rs485Send_str(timezone.dateTime("H:i"));
    }
    endDisplayUpdate();
  }
}

void loadCarousel() { loadCarouselData(); }

// Implement missing function to fix linker error
String getCurrentSettingValues() {
  JsonDocument doc;

  doc["version"] = YUELI_FIRMWARE_LABEL;
  doc["product"] = YUELI_PRODUCT_NAME;
  doc["website"] = YUELI_WEBSITE_URL;

  doc[PARAM_ALIGNMENT] = alignment;
  doc[PARAM_FLAP_SPEED] = flapSpeed;
  doc[PARAM_DEVICEMODE] = deviceMode;
  doc[PARAM_INPUT_TEXT] = inputText;
  doc[PARAM_COUNTDOWN_DATE] = countdownToDateUnix;
  doc["carouselSpeed"] = carouselInterval;

  doc["unitCount"] = ROW_WIDTH;
  doc["rowCount"] = UNITS_AMOUNT;

  // Add Scheduled Messages
  JsonArray scheduleArray = doc["scheduledMessages"].to<JsonArray>();
  for(int i = 0; i < scheduledMessages.size(); i++) {
     ScheduledMessage msg = scheduledMessages[i];
     JsonObject obj = scheduleArray.add<JsonObject>();
     obj["message"] = msg.Message;
     obj["scheduledDateTimeUnix"] = msg.ScheduledDateTimeUnix;
     obj["showIndefinitely"] = msg.ShowIndefinitely;
  }
  
  String json;
  serializeJson(doc, json);
  return json;
}
