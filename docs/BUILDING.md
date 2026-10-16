# Building the ESP32-S3 firmware

## Recorded R10 environment

The R10 build metadata records this environment:

| Component | Version / setting |
|---|---|
| Arduino CLI | 1.4.1 |
| ESP32 Arduino core | `esp32:esp32@2.0.10-cn` |
| Board | `esp32s3` |
| Flash | 16 MB, 80 MHz, DIO |
| PSRAM | 8 MB OPI |
| USB mode | Hardware CDC; CDC on boot |
| Partition scheme | `default_16MB` |

Libraries:

- AsyncTCP 1.1.4
- ESPAsyncWebServer 3.1.0
- ArduinoJson 7.4.3
- ezTime 0.8.3
- LinkedList 1.3.3

The `-cn` core is the exact recorded build input. Compatibility with every newer official ESP32 Arduino core has not been established; upgrade dependencies separately from functional changes.

The historical build configuration obtained that package through the mirror
index at
`https://jihulab.com/esp-mirror/espressif/arduino-esp32/-/raw/gh-pages/package_esp32_index_cn.json`.
The exact mirror artifact, upstream commit and full component-license inventory
were not recorded in the supplied package. Capture those values before claiming
a reproducible public binary release. See
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for the source-only
publication boundary.

## Configure local credentials

`LocalSecrets.h` is required and deliberately excluded from version control.

On Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\generate-local-secrets.ps1
```

Alternatively, copy `firmware/Master_C3/LocalSecrets.example.h` to `LocalSecrets.h` and replace both placeholders. Never commit that file.

## Arduino CLI build

With the matching core and libraries installed:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default" \
  --build-property "build.partitions=default_16MB" \
  --build-property "upload.maximum_size=6553600" \
  firmware/Master_C3
```

The recorded R10 result used 896,977 bytes of sketch space and 61,256 bytes of global-variable RAM. The current `R10.1-dev` public tree was recompiled with the same pinned environment after the security, website-identity and cache-version changes and used 897,581 bytes of sketch space and 61,256 bytes of global-variable RAM. A large unexplained difference should be investigated.

## Web assets / LittleFS

The browser UI is stored under `firmware/Master_C3/data/`. Uploading only the sketch does not necessarily update these files. Build and upload a LittleFS image that matches the partition layout, or use the complete release image when reproducing the official R10 installation.

The partition is labelled `spiffs` for Arduino compatibility but contains LittleFS data.

## Pre-release checks

1. Confirm no `LocalSecrets.h` file is tracked by Git.
2. Compile with the pinned board settings.
3. Verify the target is a 16 MB ESP32-S3 board with 8 MB OPI PSRAM.
4. Test the access point, 2.4 GHz Wi-Fi connection and current-IP reporting.
5. Verify the startup quiet period and homing lock on real hardware.
6. Verify display text, carousel, history and scheduled-message behavior.
7. Test Developer login, timeout, movement confirmation and offset readback.
8. Confirm that failure and timeout states do not claim mechanical completion.

## Reproducibility note

The original R10 source ZIP matched the source hashes recorded by the release build. This public tree externalizes credentials, so a newly built binary will not be byte-identical to the original firmware image.
