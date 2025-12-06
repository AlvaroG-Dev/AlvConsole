#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "Assets.h"
#include "Input.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

enum GameState {
  STATE_MENU,
  STATE_AIMING,
  STATE_POWER,
  STATE_SHOOTING,
  STATE_GOAL,
  STATE_MISS,
  STATE_GAMEOVER
};

enum KeeperState { KEEPER_IDLE, KEEPER_DIVE_LEFT, KEEPER_DIVE_RIGHT };

struct Vector2 {
  float x, y;
};

struct Ball {
  Vector2 pos;
  Vector2 startPos;
  Vector2 targetPos;
  float speed;
  float scale;
  bool moving;
};

class GameEngine {
public:
  GameEngine(TFT_eSPI *tft, Input *input);
  void init();
  void update(float dt);
  void draw();

private:
  TFT_eSPI *_tft;
  Input *_input;
  TFT_eSprite *_scanlineBuffer;

  static const int SCANLINE_HEIGHT = 40;

  GameState _state;
  int _score;
  int _shotsTaken;
  int _goalsScored;
  int _highScore;

  Ball _ball;

  Vector2 _keeperPos;
  KeeperState _keeperState;

  Vector2 _aimCursor;

  float _powerLevel;
  float _powerDir;
  bool _powerLocked;

  const int GOAL_X = 240;
  const int GOAL_Y = 30;
  const int GOAL_WIDTH = 140;
  const int GOAL_HEIGHT = 60;

  const int KICK_SPOT_X = 240;
  const int KICK_SPOT_Y = 280;

  void resetGame();
  void resetShot();
  void handleInput();
  void updateBall(float dt);
  void updateKeeper(float dt);
  void checkCollision();

  void renderScanline(int y, int height);
  void drawToBuffer(int offsetY);

  void drawBackground(int offsetY);
  void drawGoal(int offsetY);
  void drawKeeper(int offsetY);
  void drawPlayer(int offsetY);
  void drawBall(int offsetY);
  void drawCursor(int offsetY);
  void drawPowerBar(int offsetY);
  void drawHUD(int offsetY);
  void drawMenu(int offsetY);
  void drawResultMsg(const char *msg, uint16_t color, int offsetY);
  void drawGameOver(int offsetY);
  void drawInstructions(const char *text, int offsetY);
};

#endif
