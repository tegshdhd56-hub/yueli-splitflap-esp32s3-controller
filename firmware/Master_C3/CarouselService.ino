#include "CarouselService.h"
#include <mbedtls/sha256.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t carouselMutex = nullptr;
static bool carouselRefreshPending = false;
static int carouselCurrentIndex = -1;
static const size_t CAROUSEL_BODY_LIMIT = 32768;

void beginCarouselService() { carouselMutex = xSemaphoreCreateRecursiveMutex(); }
void lockCarousel() { xSemaphoreTakeRecursive(carouselMutex, portMAX_DELAY); }
void unlockCarousel() { xSemaphoreGiveRecursive(carouselMutex); }
void restartCarouselPlayback() {
  lockCarousel(); carouselIndex = 0; carouselCurrentIndex = -1; carouselRefreshPending = true; unlockCarousel();
}

static String carouselSnapshotLocked() {
  JsonDocument doc;
  doc["speed"] = carouselInterval;
  JsonArray messages = doc["messages"].to<JsonArray>();
  for (int i = 0; i < carouselMessages.size(); ++i) messages.add(carouselMessages.get(i));
  if (doc.overflowed()) return String();
  const size_t expected = measureJson(doc);
  String value; serializeJson(doc, value);
  return value.length() == expected ? value : String();
}
static String carouselRevision(const String& value) {
  uint8_t digest[32]; char output[65];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(value.c_str()), value.length(), digest, 0);
  for (size_t i = 0; i < 32; ++i) snprintf(output + i * 2, 3, "%02x", digest[i]);
  return String(output);
}
static bool validCarouselDocument(JsonDocument& doc) {
  if (!doc["speed"].is<long>() || doc["speed"].as<long>() < 1000 || doc["speed"].as<long>() > 60000 || !doc["messages"].is<JsonArray>()) return false;
  JsonArray messages = doc["messages"].as<JsonArray>();
  if (messages.size() > 64) return false;
  for (JsonVariant value : messages) {
    if (!value.is<const char*>()) return false;
    JsonString text = value.as<JsonString>();
    if (text.size() > 1024 || strlen(text.c_str()) != text.size()) return false;
  }
  return true;
}
void loadCarouselData() {
  if (!LittleFS.exists(carouselPath)) return;
  File file = LittleFS.open(carouselPath, "r");
  if (!file || file.size() > CAROUSEL_BODY_LIMIT) { file.close(); return; }
  JsonDocument doc;
  bool valid = !deserializeJson(doc, file) && validCarouselDocument(doc);
  file.close();
  if (!valid) { Serial.println("[Carousel] Invalid saved playlist; file retained, playback disabled."); return; }
  carouselInterval = doc["speed"].as<long>();
  carouselMessages.clear();
  for (JsonVariant message : doc["messages"].as<JsonArray>()) carouselMessages.add(message.as<String>());
}
static void carouselReply(AsyncWebServerRequest* request, int status, const char* error, const char* message) {
  JsonDocument doc; doc["error"] = error; doc["message"] = message;
  String value; serializeJson(doc, value); request->send(status, "application/json", value);
}
static void sendCarouselSnapshot(AsyncWebServerRequest* request) {
  lockCarousel();
  String value = carouselSnapshotLocked();
  JsonDocument doc;
  if (value.isEmpty() || deserializeJson(doc, value)) {
    unlockCarousel(); carouselReply(request, 503, "memory_unavailable", "Not enough memory to read the carousel. Try again."); return;
  }
  doc["revision"] = carouselRevision(value);
  doc["currentIndex"] = deviceMode == DEVICE_MODE_CAROUSEL ? carouselCurrentIndex : -1;
  doc["playing"] = deviceMode == DEVICE_MODE_CAROUSEL;
  unlockCarousel();
  if (doc.overflowed()) { carouselReply(request, 503, "memory_unavailable", "Not enough memory to read the carousel. Try again."); return; }
  const size_t expected = measureJson(doc);
  value = ""; serializeJson(doc, value);
  if (value.length() != expected) { carouselReply(request, 503, "memory_unavailable", "Not enough memory to read the carousel. Try again."); return; }
  auto* response = request->beginResponse(200, "application/json", value);
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}
static bool persistCarousel(const String& value) {
  const char* temporary = "/carousel.tmp";
  File file = LittleFS.open(temporary, "w");
  if (!file) return false;
  bool success = file.write(reinterpret_cast<const uint8_t*>(value.c_str()), value.length()) == value.length();
  file.flush(); file.close();
  if (!success) { LittleFS.remove(temporary); return false; }
  file = LittleFS.open(temporary, "r");
  success = file && file.size() == value.length() && file.readString() == value;
  file.close();
  if (!success || !LittleFS.rename(temporary, carouselPath)) { LittleFS.remove(temporary); return false; }
  return true;
}
void registerCarouselRoutes(AsyncWebServer& server) {
  server.on("/carousel", HTTP_GET, [](AsyncWebServerRequest* request) { sendCarouselSnapshot(request); });
  server.on("/carousel", HTTP_POST, [](AsyncWebServerRequest* request) {
    auto* body = static_cast<CarouselBody*>(request->_tempObject);
    if (request->contentLength() > CAROUSEL_BODY_LIMIT) { carouselReply(request, 413, "too_large", "The carousel request is too large."); return; }
    if (!body || body->invalid || body->received != body->total) { carouselReply(request, 400, "invalid_body", "Incomplete carousel request."); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body->text, body->received) || !validCarouselDocument(doc)) { carouselReply(request, 400, "invalid_carousel", "Use up to 64 text messages and a duration from 1 to 60 minutes."); return; }
    if (!doc["expectedRevision"].is<const char*>()) { carouselReply(request, 409, "revision_required", "Refresh the carousel before saving."); return; }
    if (!beginDisplayUpdate()) { sendDisplayLockedResponse(request); return; }
    lockCarousel();
    String previous = carouselSnapshotLocked();
    if (previous.isEmpty()) {
      unlockCarousel(); endDisplayUpdate(); carouselReply(request, 507, "memory_unavailable", "The previous playlist is unchanged. Try again."); return;
    }
    if (doc["expectedRevision"].as<String>() != carouselRevision(previous)) {
      unlockCarousel(); endDisplayUpdate(); carouselReply(request, 409, "carousel_changed", "The carousel changed on another page. Refresh and try again."); return;
    }
    doc.remove("expectedRevision"); doc.remove("revision"); doc.remove("currentIndex"); doc.remove("playing");
    JsonDocument clean;
    clean["speed"] = doc["speed"];
    clean["messages"] = doc["messages"];
    if (clean.overflowed() || !validCarouselDocument(clean)) {
      unlockCarousel(); endDisplayUpdate(); carouselReply(request, 507, "memory_unavailable", "The previous playlist is unchanged. Try again."); return;
    }
    const size_t expected = measureJson(clean);
    String value; serializeJson(clean, value);
    if (value.length() != expected || !persistCarousel(value)) {
      unlockCarousel(); endDisplayUpdate(); carouselReply(request, 507, "storage_failed", "The carousel could not be saved. The previous playlist is unchanged."); return;
    }
    carouselInterval = doc["speed"].as<long>();
    carouselMessages.clear();
    for (JsonVariant message : doc["messages"].as<JsonArray>()) carouselMessages.add(message.as<String>());
    carouselIndex = 0; carouselCurrentIndex = -1; carouselRefreshPending = true; lastCarouselChange = millis();
    unlockCarousel(); endDisplayUpdate();
    sendCarouselSnapshot(request);
  }, nullptr, [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    if (!total || total > CAROUSEL_BODY_LIMIT) return;
    if (index == 0) {
      auto* body = static_cast<CarouselBody*>(malloc(sizeof(CarouselBody) + total));
      if (!body) return;
      body->received = 0; body->total = total; body->invalid = false; request->_tempObject = body;
    }
    auto* body = static_cast<CarouselBody*>(request->_tempObject);
    if (!body || body->invalid) return;
    if (index != body->received || length > body->total - body->received) { body->invalid = true; return; }
    memcpy(body->text + index, data, length); body->received += length; body->text[body->received] = 0;
    // ESPAsyncWebServer frees the request-owned malloc block on disconnect/destruction.
  });
}

void checkCarousel() {
  static bool wasPlaying = false;
  if (deviceMode != DEVICE_MODE_CAROUSEL) { wasPlaying = false; return; }
  lockCarousel();
  bool changed = carouselRefreshPending || !wasPlaying;
  wasPlaying = true;
  if (carouselMessages.size() == 0) {
    if (changed) {
      inputText = "";
      inputText.reserve(ROW_WIDTH * UNITS_AMOUNT);
      for (int i = 0; i < ROW_WIDTH * UNITS_AMOUNT; ++i) inputText += ' ';
      rs485Send_str(inputText); carouselCurrentIndex = -1; carouselRefreshPending = false;
    }
  } else if (changed || uint32_t(millis() - lastCarouselChange) >= uint32_t(carouselInterval) * 60UL) {
    if (carouselIndex >= carouselMessages.size()) carouselIndex = 0;
    carouselCurrentIndex = carouselIndex;
    inputText = carouselMessages.get(carouselIndex++);
    lastCarouselChange = millis(); carouselRefreshPending = false;
    rs485Send_str(inputText);
  }
  unlockCarousel();
}
