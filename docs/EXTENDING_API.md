# Adding a customer API

## Recommended route design

Use a versioned namespace for customer integrations:

```text
/api/v1/health
/api/v1/display
/api/v1/integrations/<provider>
```

Do not overload existing UI routes unless backward compatibility is intentional.

## Where routes are registered

`firmware/Master_C3/Master_C3.ino` creates the `AsyncWebServer` as `webServer` and registers Wi-Fi, calibration, UI, history and carousel routes in `setup()`.

A clean extension pattern is:

1. Add `UserApiRoutes.h` and `UserApiRoutes.ino` to `firmware/Master_C3/`.
2. Declare `void registerUserApiRoutes(AsyncWebServer& server);` in the header.
3. Include the header from `Master_C3.ino`.
4. Call `registerUserApiRoutes(webServer);` before `webServer.begin()`.

## Minimal health endpoint

```cpp
// UserApiRoutes.h
#pragma once
#include <ESPAsyncWebServer.h>

void registerUserApiRoutes(AsyncWebServer& server);
```

```cpp
// UserApiRoutes.ino
#include "UserApiRoutes.h"
#include <ArduinoJson.h>

void registerUserApiRoutes(AsyncWebServer& server) {
  server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["version"] = YUELI_FIRMWARE_VERSION;
    doc["uptimeMs"] = millis();

    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* response =
      request->beginResponse(200, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    request->send(response);
  });
}
```

## JSON display endpoint pattern

Before sending movement-related data:

1. Enforce a request-size limit before allocation.
2. Parse and validate the complete body.
3. Authenticate the request if any untrusted client can reach the device.
4. Call `beginDisplayUpdate()` and return the existing lock response when it fails.
5. Send the update.
6. Always call `endDisplayUpdate()` after a successful lock.
7. Return `202 Accepted`, not “completed,” when the device has only accepted/transmitted the command.

Illustrative handler core:

```cpp
if (!beginDisplayUpdate()) {
  sendDisplayLockedResponse(request);
  return;
}

inputText = validatedText;
rs485Send_str(inputText);
endDisplayUpdate();

request->send(
  202,
  "application/json",
  "{\"status\":\"accepted\",\"mechanicalCompletion\":false}"
);
```

Do not call `endDisplayUpdate()` if `beginDisplayUpdate()` failed, because the lock was not acquired.

## Authentication choices

The current Developer bearer-token verifier is private to `CalibrationService.ino`; do not copy the token into URLs or logs. For a customer API, choose and document one of these boundaries:

- trusted-LAN only, enforced by network segmentation;
- an authenticated TLS reverse proxy or VPN in front of the controller;
- a dedicated API credential stored as a digest in `LocalSecrets.h`, compared in constant time;
- a redesigned device pairing flow with per-device credentials.

Do not place API keys in query strings. Do not hard-code real credentials in tracked source.

## Handler safety

- Keep callbacks short and non-blocking.
- Queue Wi-Fi scans/connections and long filesystem operations for the main loop.
- Bound every JSON or text request before allocation.
- Reject embedded NUL characters and unexpected encodings.
- Validate `dip`, `page`, `steps`, `offset` and enum ranges.
- Use temporary-file + readback + rename for persistent configuration.
- Do not issue concurrent PY32 offset reads; the wire protocol has no transaction ID.
- Preserve startup homing and calibration movement locks.

## API documentation checklist

For every added route, document:

- method and path;
- authentication boundary;
- request content type and maximum size;
- parameters and allowed ranges;
- successful response and status code;
- failure codes;
- whether the response means accepted, transmitted, persisted or physically completed;
- compatibility and migration behavior.

## Example integrations

Common integrations that fit this API boundary include:

- Home Assistant or Node-RED message updates;
- restaurant menu and queue systems;
- hotel welcome messages;
- office KPIs and milestones;
- calendar, weather or internal webhook gateways.

Fetch third-party data on a gateway where practical, then send validated text to the controller. This keeps cloud credentials and TLS complexity off the embedded device.
