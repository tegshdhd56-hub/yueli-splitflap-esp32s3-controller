#ifndef MASTER_CONSTANTS_H
#define MASTER_CONSTANTS_H

// YUELI Split-Flap controller firmware — https://www.yueli.hk/

#if __has_include("LocalSecrets.h")
#include "LocalSecrets.h"
#else
#error "Missing LocalSecrets.h. Copy LocalSecrets.example.h to LocalSecrets.h and replace every placeholder before compiling."
#endif

#if !defined(YUELI_SECRETS_CONFIGURED) || YUELI_SECRETS_CONFIGURED != 1
#error "LocalSecrets.h is still a template. Generate unique deployment credentials before compiling."
#endif

// System Configuration
#define YUELI_PRODUCT_NAME "YUELI Split-Flap"
#define YUELI_FIRMWARE_VERSION "R10.1-dev"
#define YUELI_FIRMWARE_LABEL "YUELI R10.1-dev"
#define YUELI_ASSET_VERSION "r10-1-dev"
#define YUELI_WEBSITE_URL "https://www.yueli.hk/"
#define SERIAL_ENABLE true
#define SERIAL_BAUDRATE 115200
#define SERIAL1_BAUDRATE 19200
#define UNITS_AMOUNT 6  // Number of rows (Slaves)
#define FLAP_AMOUNT 64  // Characters per unit
#define ROW_WIDTH 23    // Characters per row

// WiFi / Network Configuration
#define MASTER_AP_SSID "YUELI"
#define TCP_PORT 8888

// Speed Limits
#define MIN_SPEED 1
#define MAX_SPEED 12

// Display Settings
const int scheduledMessageDisplayTimeMillis = 7500;

#endif
