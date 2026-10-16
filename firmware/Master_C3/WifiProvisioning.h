#ifndef YUELI_WIFI_PROVISIONING_H
#define YUELI_WIFI_PROVISIONING_H
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
// Fixed-size cross-thread messages; Wi-Fi and flash work run only in loop().
const uint8_t WIFI_NETWORK_LIMIT = 32;
struct ProvisionNetwork { char ssid[33]; int32_t rssi; bool secure; };
struct ProvisionStatus {
  char state[20]; char ssid[33]; char ip[16]; char message[160];
  bool connected; char currentSsid[33]; char currentIp[16];
  char clientRequestId[65];
};
struct ProvisionScan {
  char state[12]; uint8_t count; ProvisionNetwork networks[WIFI_NETWORK_LIMIT];
  char message[160]; char errorCode[40];
  int32_t driverError; int32_t completionStatus;
  uint32_t elapsedMs; uint32_t startedAt; uint32_t requestId;
  uint16_t scannedCount; uint8_t scanId; bool cleanupPending;
};
struct ProvisionRequest { char ssid[33]; char password[65]; char clientRequestId[65]; };
void initWiFi();
void serviceWiFi();
void registerWiFiRoutes(AsyncWebServer& server);
#endif
