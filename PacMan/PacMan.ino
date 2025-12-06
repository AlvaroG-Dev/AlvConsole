#include "GameEngine.h"
#include "Input.h"
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
  Serial.println("Pac-Man Starting...");

  // Init Display
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.begin();
  tft.setRotation(3); // Landscape
  tft.fillScreen(TFT_BLACK);

  // Init Input
  input.begin();

  // Init Game Engine
  engine.init();

  lastTime = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0f;
  lastTime = now;

  if (dt > 0.1f)
    dt = 0.1f;

  engine.update(dt);
  engine.draw();
}