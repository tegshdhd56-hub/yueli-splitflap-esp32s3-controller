#include <Arduino.h>
#include "Constants.h"
#include "Classes.h"

//Aligns string on center of array and fills empty chars with spaces
String cleanString(String message);

//Aligns string on center of array and fills empty chars with spaces
//Aligns string on center of array and fills empty chars with spaces
String centerString(String message) {
  //Takes care of the left side
  int leftSpaceAmount = (ROW_WIDTH -  message.length()) / 2;
  for (int spaceIndex = 0; spaceIndex < leftSpaceAmount; spaceIndex++) {
    message = " " + message;
  }

  //Take care of the right side
  for(int spaceIndex = message.length(); spaceIndex < ROW_WIDTH; spaceIndex++) {
    message = message + " ";
  }

  message = cleanString(message);
  
  return message;
}

String createRepeatingString(char character) {
  String newMessage = "";
  for (int unitIndex = 0; unitIndex < ROW_WIDTH; unitIndex++) {
    newMessage.concat(character);
  }

  return newMessage;  
}

//Aligns string on right side of array and fills empty chars with spaces
String rightString(String message) {
  int rightSpaceAmount = (ROW_WIDTH - message.length());
  for (int spaceIndex = 0; spaceIndex < rightSpaceAmount; spaceIndex++) {
    message = " " + message;
  }

  message = cleanString(message);

  return message;
}

//Aligns string on left side of array and fills empty chars with spaces
String leftString(String message) {
  int leftSpaceAmount = (ROW_WIDTH - message.length());
  for (int spaceIndex = 0; spaceIndex < leftSpaceAmount; spaceIndex++) {
    message = message + " ";
  }

  message = cleanString(message);

  return message;
}

//Converts input string to uppercase
String cleanString(String message) {
  message.toUpperCase();
  
  return message;
}

int convertSpeed(String flapSpeed) {
  int flapSpeedInt;
  flapSpeedInt = flapSpeed.toInt();
  flapSpeedInt = map(flapSpeedInt, 1, 100, MIN_SPEED, MAX_SPEED);
  
  return flapSpeedInt;
}

LList<String> processSentenceToLines(String sentence) {    
  Serial.println("[processSentenceToLines] Input sentence: '" + sentence + "'");
  Serial.println("[processSentenceToLines] Sentence length: " + String(sentence.length()));
  
  //Remove trailing and preceding whitespace
  sentence.trim();
  
  //Handle newline characters being passed up
  sentence.replace("\\n", "\n");
  
  Serial.println("[processSentenceToLines] After \\n replacement: '" + sentence + "'");
  
  //No sentence to start with a newline
  if (sentence.startsWith("\n")) {
    sentence = sentence.substring(1);
  }

  //Replace any double newlines with a single one, not allowing anymore then one
  while(sentence.indexOf("\n\n") != -1) 
  {
    sentence.replace("\n\n", "\n");
  }

  //Remove pre-ceding space to a newline
  while(sentence.indexOf(" \n") != -1) 
  {
    sentence.replace(" \n", "\n");
  }

  //Replace all double spaces
  while(sentence.indexOf("  ") != -1) 
  {
    sentence.replace("  ", " ");
  }
  
  // Split by newlines first - this is the main issue!
  LList<String> lines;
  int startIdx = 0;
  int newlineIdx = 0;
  
  while ((newlineIdx = sentence.indexOf('\n', startIdx)) != -1) {
    String line = sentence.substring(startIdx, newlineIdx);
    line.trim();
    if (line.length() > 0) {
      lines.add(line);
    }
    startIdx = newlineIdx + 1;
  }
  
  // Add the last line
  String lastLine = sentence.substring(startIdx);
  lastLine.trim();
  if (lastLine.length() > 0) {
    lines.add(lastLine);
  }
  
  Serial.println("[processSentenceToLines] Lines after split: " + String(lines.size()));
  for (int i = 0; i < lines.size(); i++) {
    Serial.println("[processSentenceToLines] Line " + String(i) + ": '" + lines[i] + "'");
  }
  
  return lines;
}

//Used for ensuring a string is a number
bool isNumber(String str) {
  char* endPtr;

  //10 specifies base 10 (decimal)
  strtol(str.c_str(), &endPtr, 10);  

  //Check if the conversion reached the end of the string
  return (*endPtr == '\0');
}