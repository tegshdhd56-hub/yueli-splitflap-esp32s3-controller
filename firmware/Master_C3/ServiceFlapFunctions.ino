#include <Arduino.h>
#include <AsyncTCP.h>
#include "Constants.h"
#include "Classes.h"
#include "CalibrationService.h"

extern AsyncClient* slaveClients[UNITS_AMOUNT];
extern bool slaveConnected[UNITS_AMOUNT];
extern String lastWrittenText;
extern bool alignmentUpdated;
extern String alignment;
extern String flapSpeed;

// Externs for Helpers
extern LList<String> processSentenceToLines(String sentence);
extern String centerString(String message);
extern String leftString(String message);
extern String rightString(String message);
extern int convertSpeed(String speed);

// Forward Declarations
void showMessage(String message, int flapSpeed);
void sendToSlave(int row, String text, int speed);

// Shows a new message on the display
void showText(String message) {  
  showText(message, 0);
}

void showText(String message, int delayMillis) {  
  if (lastWrittenText != message || alignmentUpdated) { 
    Serial.println("Showing new Message: " + message);

    showMessage(message, convertSpeed(flapSpeed));

    if (delayMillis != 0) delay(delayMillis);

    lastWrittenText = message;
    alignmentUpdated = false;
  }
}

// Pushes message to units via TCP
void showMessage(String message, int flapSpeed) {
  // Debug: Print original message details
  Serial.println("[DEBUG] Original message: " + message);
  Serial.println("[DEBUG] Message length: " + String(message.length()));
  
  // Step 1: Split message into lines (Rows)
  LList<String> lines = processSentenceToLines(message);
  
  // Debug: Print lines details
  Serial.println("[DEBUG] Lines size: " + String(lines.size()));
  for (int i = 0; i < lines.size(); i++) {
    Serial.println("[DEBUG] Line " + String(i) + ": '" + lines[i] + "' (length: " + String(lines[i].length()) + ")");
  }
  
  // Step 2: Iterate over each physical row (0 to 3)
  for (int i = 0; i < UNITS_AMOUNT; i++) {
    String lineText = "";
    if (i < lines.size()) {
       lineText = lines[i];
       Serial.printf("lines: %s \n" , lines[i]);
    }
    Serial.printf("line length: %d" , lineText.length());


    // Step 3: Align the text within the row (16 chars)
    // Forced Right Alignment
    lineText = rightString(lineText);

    // Ensure it is exactly ROW_WIDTH (16) chars just in case
    // (Alignment functions should handle this, but safety first)
    if (lineText.length() > ROW_WIDTH) {
       lineText = lineText.substring(0, ROW_WIDTH);
    }
    // while (lineText.length() < ROW_WIDTH) {
    //    lineText += " ";
    // }

    // Step 4: Send to Slave
    sendToSlave(i, lineText, flapSpeed);
  }
}

void sendToSlave(int row, String text, int speed) {
  if (!slaveConnected[row] || slaveClients[row] == NULL) {
    // Serial.printf("Row %d Disconnected. Skip.\n", row);
    return;
  }
  
  if (!slaveClients[row]->connected()) {
     slaveConnected[row] = false;
     return;
  }

  // Protocol: CMD:{TEXT}\n
  // text is exactly 16 chars.
  String packet = "CMD:" + text + "\n";
  if (!beginDisplayUpdate()) return;
  slaveClients[row]->write(packet.c_str());
  endDisplayUpdate();
  Serial.printf("[TCP] Sent to Row %d: %s", row, packet.c_str());
}

bool isDisplayMoving() {
  // Can implement query logic if needed
  return false;
}
