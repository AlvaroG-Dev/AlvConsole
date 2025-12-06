#include "GameEngine.h"
#include "Input.h"
#include "System.h"
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

// Pin Definitions
#define TFT_BL 7

TFT_eSPI tft = TFT_eSPI();
Input input;
GameEngine engine(&tft, &input);

unsigned long lastTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=================================");
  Serial.println("Penalty Game Starting...");
  Serial.println("=================================");

  // Init Display
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  Serial.println("Initializing TFT...");
  tft.begin();
  tft.setRotation(3);      // Landscape
  tft.fillScreen(TFT_RED); // Red screen to test
  delay(500);
  tft.fillScreen(TFT_GREEN); // Green screen to test
  delay(500);
  tft.fillScreen(TFT_BLUE); // Blue screen to test
  delay(500);

  Serial.println("TFT OK - Colors tested");

  // Init Input
  Serial.println("Initializing Input...");
  input.begin();
  Serial.println("Input OK");

  // Init Game Engine
  Serial.println("Initializing Game Engine...");
  engine.init();
  Serial.println("Game Engine OK");

  lastTime = millis();
  Serial.println("Setup complete!");
  Serial.println("Free heap: " + String(ESP.getFreeHeap()));
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0f;
  lastTime = now;

  // Cap dt to avoid huge jumps
  if (dt > 0.1f)
    dt = 0.1f;

  engine.update(dt);
  engine.draw();

  // Debug every 2 seconds
  static unsigned long lastDebug = 0;
  if (now - lastDebug > 2000) {
    Serial.println("Loop running... Free heap: " + String(ESP.getFreeHeap()));
    lastDebug = now;
  }
}
