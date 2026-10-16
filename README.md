# YUELI Split-Flap ESP32-S3 Controller

Open-source controller firmware and local HTTP API for a YUELI mechanical split-flap display. The firmware runs on the Waveshare ESP32-S3-RS485-CAN board and coordinates the web interface, Wi-Fi provisioning, message scheduling, carousel playback, developer calibration, and RS485 communication with split-flap units.

Learn about the finished display at [YUELI mechanical split-flap displays](https://www.yueli.hk/) or see [custom split-flap options](https://www.yueli.hk/pages/customize).

> **R10 verification status:** the private historical source archive matched its recorded R10 build inputs. Its binary was compiled and statically checked, but was not flashed to or tested on physical hardware. Treat real-device validation as required before production deployment.

> **Source status:** this public `R10.1-dev` tree contains post-R10 security and
> website-identity changes. Historical R10 archives are intentionally not
> published because they contain legacy shared credentials and have unresolved
> third-party redistribution obligations.

## 中文简介

这是 YUELI Splitflap 总控的 ESP32-S3 固件源码，负责网页控制、Wi-Fi 配网、文字与轮播播放、预约消息、Developer 校准，以及通过 RS485 控制翻页单元。仓库提供现有接口说明和增加自定义 HTTP API 的方法。

R10 完整镜像会覆盖整块 16 MB Flash，并清除 ESP32 上保存的 Wi-Fi、历史、轮播及预约数据。量产或客户设备升级前必须先在测试机验证并备份数据。

## Hardware

- Controller: Waveshare ESP32-S3-RS485-CAN
- Flash: 16 MB
- PSRAM: 8 MB OPI
- RS485 UART: 19200 baud, 8N1
- ESP32-S3 pins: RX GPIO18, TX GPIO17, direction GPIO21
- Matrix defaults: 23 columns × 6 rows
- Unit addresses used by the default matrix: DIP 0-137

## Features

- Browser-based text editor and display control
- Device access point plus 2.4 GHz Wi-Fi provisioning
- Local HTTP endpoints and TCP port 8888
- Message history, scheduling and carousel playback
- Startup quiet period and homing workflow
- Authenticated Developer calibration workflow
- PY32 unit communication over an ASCII RS485 protocol
- Extension guide for customer-specific APIs and integrations

## Repository layout

```text
firmware/Master_C3/       ESP32-S3 Arduino firmware and web assets
reference/PY32/           PY32 separation and authorized-source notice
docs/API.md               Existing local HTTP API
docs/EXTENDING_API.md     How to add a customer API safely
docs/BUILDING.md          Toolchain and build instructions
docs/FLASHING.md          R10 binary flashing and recovery
tools/                    Local credential helper
releases/                 Public release policy; no legacy binary archives
```

## Quick start from source

1. Install the pinned toolchain and libraries in [docs/BUILDING.md](docs/BUILDING.md).
2. Generate unique local credentials:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\generate-local-secrets.ps1
   ```

3. Compile `firmware/Master_C3/Master_C3.ino` for the ESP32-S3 board configuration documented in the build guide.
4. Upload the sketch and the `data/` LittleFS assets.
5. Keep the controller on a trusted local network; do not expose port 80 or 8888 directly to the Internet.

## Using the existing API

The firmware already exposes status, Wi-Fi, history, carousel, display and developer-calibration routes. Start with [docs/API.md](docs/API.md).

Example: update the displayed text on a controller reachable at `192.168.4.1`:

```bash
curl -X POST http://192.168.4.1/ \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode "deviceMode=text" \
  --data-urlencode "inputText=HELLO WORLD"
```

The legacy root endpoint redirects to the UI after a successful write. For a JSON-first endpoint under `/api/v1/`, follow [docs/EXTENDING_API.md](docs/EXTENDING_API.md).

## Safety and security

- Generate a unique AP password and Developer password for every deployment.
- `LocalSecrets.h` is intentionally ignored by Git.
- The archived R10 binary retains its legacy shared AP credential; rebuild before customer deployment.
- General display and Wi-Fi routes are intended for a trusted LAN and are not all authenticated.
- The device serves plain HTTP and has no TLS termination.
- RS485 movement commands do not provide reliable mechanical-completion acknowledgements.
- Do not infer successful movement or Flash persistence solely from an HTTP response.

See [SECURITY.md](SECURITY.md) before deployment.

## Release binaries

This public repository currently distributes source only. Historical R10
archives are withheld because they contain legacy shared credentials, were not
validated on hardware, and require a separate third-party licensing review.
See [the release policy](releases/README.md) and [docs/FLASHING.md](docs/FLASHING.md).

PY32 unit firmware is a different firmware target and must never be flashed to
the ESP32-S3. Third-party PY32 files are not redistributed here; obtain them
through an authorized hardware/vendor channel.

## Contributing

Issues and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md), keep API changes backward compatible where practical, and document any new route, authentication requirement or RS485 command.

## License

Copyright 2026 YUELI. Licensed under the [Apache License 2.0](LICENSE).
The attribution information in [NOTICE](NOTICE) must be preserved when required
by that license.

The Apache-2.0 license covers YUELI-authored source and documentation. See the
local README under `reference/PY32/` for the separate PY32 redistribution
boundary.

Build dependencies retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No prebuilt firmware image is
distributed by this repository at present.

## Trademark

YUELI and the YUELI logo identify the product and its manufacturer. An open-source software license does not grant permission to use YUELI trademarks to imply endorsement or origin of modified hardware.
