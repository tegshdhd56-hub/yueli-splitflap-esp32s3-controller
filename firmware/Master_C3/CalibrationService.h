#ifndef YUELI_CALIBRATION_SERVICE_H
#define YUELI_CALIBRATION_SERVICE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void beginCalibrationService();
void registerCalibrationRoutes(AsyncWebServer& server);
void serviceCalibration();
bool calibrationDisplayLocked();
// A successful begin holds the recursive UART mutex until endDisplayUpdate().
bool beginDisplayUpdate();
void endDisplayUpdate();
bool sendDisplayRS485(const String& command);
void sendDisplayLockedResponse(AsyncWebServerRequest* request);

struct CalibrationCommand {
  uint32_t id;
  char text[48];
  char action[8];
  uint8_t dip;
  int8_t page; // -1 for commands without a page argument.
  bool invalidatesOffset;
  bool flashWrite;
  bool motion;
  uint32_t minimumDelayMs;
  int32_t expectedOffset;
};
struct CalibrationRxLine {
  uint32_t at;
  char text[128];
};

#endif
