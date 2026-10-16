#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Constants.h"
#include "Classes.h"

extern String countdownToDateUnix;
extern String alignment;
extern String flapSpeed;
extern String deviceMode;
extern LList<ScheduledMessage> scheduledMessages;

// Files for persistence (defined in Master_C3)
extern const char* alignmentPath;
extern const char* flapSpeedPath;
extern const char* deviceModePath;
extern const char* countdownPath;
extern const char* scheduledMessagesPath;

extern const char* ALIGNMENT_MODE_DEFAULT;
extern const char* DEVICE_MODE_TEXT;
extern void writeFile(fs::FS &fs, const char * path, const char * message);
extern String readWholeFile(fs::FS &fs, const char * path, String defaultValue);

//Initialize LittleFS
void initialiseFileSystem() {
  if (!LittleFS.begin()) {
    SerialPrintln("An error has occurred while mounting LittleFS");
  }

  SerialPrintln("LittleFS mounted successfully");
}

//Load variables that are saved to the file system
void loadValuesFromFileSystem() {
  //Load values saved in LittleFS
  countdownToDateUnix = readFile(LittleFS, countdownPath, "0");
  alignment = readFile(LittleFS, alignmentPath, ALIGNMENT_MODE_DEFAULT);
  flapSpeed = readFile(LittleFS, flapSpeedPath, "80");
  deviceMode = readFile(LittleFS, deviceModePath, DEVICE_MODE_TEXT);

  String scheduledMessagesJson = readFile(LittleFS, scheduledMessagesPath, "");
  if (scheduledMessagesJson != "") {    
    SerialPrintln("Loading Scheduled Messages");
    readScheduledMessagesFromJson(scheduledMessagesJson);
  }
  
  SerialPrintln("Loaded Settings:");
  SerialPrintln("   Alignment: " + alignment);
  SerialPrintln("   Flap Speed: " + flapSpeed);
  SerialPrintln("   Device Mode: " + deviceMode);
  SerialPrintln("   Countdown to Date UNIX: " + countdownToDateUnix);
  SerialPrint("   Scheduled Message Count: ");
  SerialPrintln(scheduledMessages.size());
}

// Read only bytes that exist. Stream::readStringUntil waits for its timeout at
// EOF when a settings file has no trailing newline; our writer omits newlines.
static String readStoredFile(File& file, bool firstLineOnly) {
  const size_t expected = file.size();
  String result;
  if (!result.reserve(expected)) return String();
  uint8_t buffer[257]; // Arduino String::concat also copies a trailing terminator.
  size_t remaining = expected;
  while (remaining) {
    const size_t wanted = remaining < 256 ? remaining : 256;
    const size_t received = file.read(buffer, wanted);
    if (!received) return String();
    remaining -= received;
    size_t keep = received;
    if (firstLineOnly) {
      const void* newline = memchr(buffer, '\n', received);
      if (newline) keep = static_cast<const uint8_t*>(newline) - buffer;
    }
    buffer[keep] = 0;
    if (!result.concat(reinterpret_cast<const char*>(buffer), keep)) return String();
    if (keep != received) break;
  }
  return result;
}

//Read File from LittleFS
String readFile(fs::FS &fs, const char * path, String defaultValue) {
  SerialPrintf("Reading file: %s\r\n", path);

  File file = fs.open(path, "r");
  if (!file || file.isDirectory()) {
    SerialPrintln("- Failed to open file for reading");

    if (defaultValue.length() > 0) {
      SerialPrintln("- Writing a default value as one has been specified. Default: " + defaultValue);
      writeFile(LittleFS, path, defaultValue.c_str());
      
      return defaultValue;
    }
    
    return String();
  }

  return readStoredFile(file, true);
}

//Write file to LittleFS
void writeFile(fs::FS &fs, const char * path, const char * message) {
  SerialPrintf("Writing file: %s\r\n", path);

  File file = fs.open(path, "w");
  if (!file) {
    SerialPrintln("- Failed to open file for writing");
    return;
  }
  
  if (file.print(message)) {
    SerialPrintln("- File written");
  } 
  else {
    SerialPrintln("- Write failed");
  }
}

//Read entire file (multi-line) from LittleFS
String readWholeFile(fs::FS &fs, const char * path, String defaultValue) {
  SerialPrintf("Reading whole file: %s\r\n", path);

  File file = fs.open(path, "r");
  if (!file || file.isDirectory()) {
    SerialPrintln("- Failed to open file for reading");
    if (defaultValue.length() > 0) {
      writeFile(LittleFS, path, defaultValue.c_str());
      return defaultValue;
    }
    return String();
  }

  return readStoredFile(file, false);
}
