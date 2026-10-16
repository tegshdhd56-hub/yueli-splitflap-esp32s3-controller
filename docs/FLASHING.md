# Flashing R10

## Warning

The complete image overwrites the full 16 MB ESP32-S3 Flash and resets stored Wi-Fi configuration, history, carousel entries, scheduled messages and other ESP32 data. Back up the device before upgrading if those records matter.

The recorded R10 release was compiled and statically checked but was not flashed to or tested on physical hardware when packaged.

## Supported controller

- Waveshare ESP32-S3-RS485-CAN
- 16 MB Flash
- 8 MB OPI PSRAM

## Complete image — recommended

From the extracted release directory, replace `PORT` with the actual serial port:

```bash
python -m esptool --chip esp32s3 --port PORT flash_id
python -m esptool --chip esp32s3 --port PORT --baud 460800 read_flash 0 ALL backup_before_flash.bin
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 firmware_all_0x0.bin
python -m esptool --chip esp32s3 --port PORT --baud 460800 verify_flash 0x0 firmware_all_0x0.bin
```

Use an esptool version whose command spelling matches the examples, or translate to the equivalent hyphenated esptool 5.x commands.

## Segment addresses — advanced users only

Use either the complete image or the segment set, not both in the same operation.

| Address | File |
|---:|---|
| `0x000000` | `segments/bootloader.bin` |
| `0x008000` | `segments/partitions.bin` |
| `0x00E000` | `segments/boot_app0.bin` |
| `0x010000` | `segments/app.bin` |
| `0xC90000` | `segments/littlefs.bin` |

The LittleFS partition size is `0x360000` bytes. Do not combine these files with an older partition table or older filesystem image.

## Recovery

If the board cannot enter download mode:

1. Disconnect other power sources and use one known-good USB data cable.
2. Hold **BOOT**.
3. Press and release **RESET**.
4. Release **BOOT**.
5. Recheck the serial port and retry at 115200 baud.

## Firmware target separation

`firmware_all_0x0.bin` and the segment images are for the ESP32-S3 controller.
PY32F002A unit firmware is a separate target and is not redistributed in this
public repository. Never flash one target's image to the other target.

PY32 unit updates may affect activation and calibration data. Follow the hardware vendor's process and preserve required authorization and calibration state.
