#include "GameEngine.h"

GameEngine::GameEngine(TFT_eSPI *tft, Input *input) : _tft(tft), _input(input) {
  _scanlineBuffer = nullptr;
  _state = STATE_MENU;
  _highScore = 0;
}

void GameEngine::init() {
  Serial.println("GameEngine::init() - Scanline rendering mode");

  _scanlineBuffer = new TFT_eSprite(_tft);
  if (_scanlineBuffer) {
    bool created = _scanlineBuffer->createSprite(480, SCANLINE_HEIGHT);
    if (created) {
      Serial.printf("Scanline buffer created: 480x%d\n", SCANLINE_HEIGHT);
      _scanlineBuffer->setSwapBytes(true);
    } else {
      Serial.println("ERROR: Failed to create scanline buffer!");
      delete _scanlineBuffer;
      _scanlineBuffer = nullptr;
    }
  }

  resetGame();
  Serial.println("GameEngine initialized");
}

void GameEngine::resetGame() {
  _score = 0;
  _shotsTaken = 0;
  _goalsScored = 0;
  _state = STATE_MENU;
  resetShot();
}

void GameEngine::resetShot() {
  _ball.pos.x = KICK_SPOT_X;
  _ball.pos.y = KICK_SPOT_Y;
  _ball.startPos = _ball.pos;
  _ball.moving = false;
  _ball.scale = 1.0f;

  _aimCursor.x = GOAL_X;
  _aimCursor.y = GOAL_Y + GOAL_HEIGHT / 2;

  _keeperPos.x = GOAL_X;
  _keeperPos.y = GOAL_Y + GOAL_HEIGHT - 12;
  _keeperState = KEEPER_IDLE;

  _powerLevel = 0.0f;
  _powerDir = 1.0f;
  _powerLocked = false;

  if (_state != STATE_MENU && _state != STATE_GAMEOVER) {
    _state = STATE_AIMING;
  }
}

void GameEngine::update(float dt) {
  handleInput();

  switch (_state) {
  case STATE_AIMING: {
    JoystickInput joy = _input->getJoystick();
    if (joy.active) {
      _aimCursor.x += (joy.x / 400.0f) * 6.0f;
      _aimCursor.y += (joy.y / 400.0f) * 6.0f;

      _aimCursor.x = constrain(_aimCursor.x, GOAL_X - GOAL_WIDTH / 2 + 12,
                               GOAL_X + GOAL_WIDTH / 2 - 12);
      _aimCursor.y =
          constrain(_aimCursor.y, GOAL_Y + 12, GOAL_Y + GOAL_HEIGHT - 12);
    }
  } break;

  case STATE_POWER:
    _powerLevel += _powerDir * dt * 2.5f;
    if (_powerLevel >= 1.0f) {
      _powerLevel = 1.0f;
      _powerDir = -1.0f;
    } else if (_powerLevel <= 0.0f) {
      _powerLevel = 0.0f;
      _powerDir = 1.0f;
    }
    break;

  case STATE_SHOOTING:
    updateBall(dt);
    updateKeeper(dt);
    break;

  case STATE_GOAL:
  case STATE_MISS: {
    static float timer = 0;
    timer += dt;
    if (timer > 2.0f) {
      timer = 0;
      _shotsTaken++;
      if (_shotsTaken >= 5) {
        _state = STATE_GAMEOVER;
      } else {
        resetShot();
      }
    }
  } break;

  case STATE_GAMEOVER:
  case STATE_MENU:
    break;
  }
}

void GameEngine::handleInput() {
  ButtonInput btn = _input->getButtons();

  if (btn.aJustPressed) {
    switch (_state) {
    case STATE_MENU:
      _state = STATE_AIMING;
      resetShot();
      _shotsTaken = 0;
      _goalsScored = 0;
      _score = 0;
      break;

    case STATE_AIMING:
      _state = STATE_POWER;
      break;

    case STATE_POWER: {
      _state = STATE_SHOOTING;
      _ball.moving = true;
      _ball.targetPos = _aimCursor;

      int r = random(100);
      int difficulty = 45;

      if (r < difficulty) {
        if (_aimCursor.x < GOAL_X - 30) {
          _keeperState = KEEPER_DIVE_LEFT;
        } else if (_aimCursor.x > GOAL_X + 30) {
          _keeperState = KEEPER_DIVE_RIGHT;
        } else {
          _keeperState =
              (random(2) == 0) ? KEEPER_DIVE_LEFT : KEEPER_DIVE_RIGHT;
        }
      } else {
        if (random(2) == 0) {
          _keeperState = KEEPER_IDLE;
        } else {
          if (_aimCursor.x < GOAL_X) {
            _keeperState = KEEPER_DIVE_RIGHT;
          } else {
            _keeperState = KEEPER_DIVE_LEFT;
          }
        }
      }
      break;
    }

    case STATE_GAMEOVER:
      resetGame();
      break;

    default:
      break;
    }
  }

  if (btn.bJustPressed && _state != STATE_MENU) {
    resetGame();
  }
}

void GameEngine::updateBall(float dt) {
  if (!_ball.moving)
    return;

  float speed = 300.0f + (_powerLevel * 200.0f);

  Vector2 dir;
  dir.x = _ball.targetPos.x - _ball.pos.x;
  dir.y = _ball.targetPos.y - _ball.pos.y;

  float dist = sqrt(dir.x * dir.x + dir.y * dir.y);

  if (dist < 5.0f) {
    _ball.pos = _ball.targetPos;
    checkCollision();
  } else {
    dir.x /= dist;
    dir.y /= dist;

    _ball.pos.x += dir.x * speed * dt;
    _ball.pos.y += dir.y * speed * dt;

    float progress = (KICK_SPOT_Y - _ball.pos.y) / (KICK_SPOT_Y - GOAL_Y);
    _ball.scale = 1.0f - (progress * 0.7f);
  }
}

void GameEngine::updateKeeper(float dt) {
  float speed = 180.0f;

  if (_keeperState == KEEPER_DIVE_LEFT) {
    _keeperPos.x -= speed * dt;
    if (_keeperPos.x < GOAL_X - 60)
      _keeperPos.x = GOAL_X - 60;
  } else if (_keeperState == KEEPER_DIVE_RIGHT) {
    _keeperPos.x += speed * dt;
    if (_keeperPos.x > GOAL_X + 60)
      _keeperPos.x = GOAL_X + 60;
  }
}

void GameEngine::checkCollision() {
  _ball.moving = false;

  float kx = _keeperPos.x;
  float ky = _keeperPos.y;
  float kw = 35;
  float kh = 35;

  float bx = _ball.pos.x;
  float by = _ball.pos.y;

  bool blocked = false;

  if (bx > kx - kw / 2 && bx < kx + kw / 2 && by > ky - kh && by < ky + 10) {
    blocked = true;
  }

  bool inGoal = (bx > GOAL_X - GOAL_WIDTH / 2 && bx < GOAL_X + GOAL_WIDTH / 2 &&
                 by > GOAL_Y && by < GOAL_Y + GOAL_HEIGHT);

  if (!inGoal) {
    _state = STATE_MISS;
  } else if (blocked) {
    _state = STATE_MISS;
  } else {
    _state = STATE_GOAL;
    _goalsScored++;
    int bonus = (int)(_powerLevel * 100);
    _score += 100 + bonus;
  }
}

void GameEngine::draw() {
  if (!_scanlineBuffer) {
    _tft->fillScreen(C_GRASS);
    return;
  }

  for (int y = 0; y < 320; y += SCANLINE_HEIGHT) {
    int height = SCANLINE_HEIGHT;
    if (y + height > 320) {
      height = 320 - y;
    }

    renderScanline(y, height);
  }
}

void GameEngine::renderScanline(int y, int height) {
  _scanlineBuffer->fillSprite(C_GRASS);
  drawToBuffer(y);
  _scanlineBuffer->pushSprite(0, y, 0, 0, 480, height);
}

void GameEngine::drawToBuffer(int offsetY) {
  drawBackground(offsetY);
  drawGoal(offsetY);

  if (_state != STATE_SHOOTING && _state != STATE_GOAL &&
      _state != STATE_MISS) {
    drawPlayer(offsetY);
  }

  drawKeeper(offsetY);

  // Only draw ball during shooting
  if (_state == STATE_SHOOTING) {
    drawBall(offsetY);
  }

  if (_state == STATE_AIMING) {
    drawCursor(offsetY);
    drawInstructions("Joystick: Aim | A: Lock", offsetY);
  }

  if (_state == STATE_POWER) {
    drawCursor(offsetY);
    drawPowerBar(offsetY);
    drawInstructions("A: SHOOT! | B: Cancel", offsetY);
  }

  drawHUD(offsetY);

  if (_state == STATE_MENU)
    drawMenu(offsetY);
  if (_state == STATE_GOAL)
    drawResultMsg("GOAL!", C_GREEN, offsetY);
  if (_state == STATE_MISS)
    drawResultMsg("SAVED!", C_RED, offsetY);
  if (_state == STATE_GAMEOVER)
    drawGameOver(offsetY);
}

void GameEngine::drawBackground(int offsetY) {
  // Sky with gradient effect
  if (offsetY < 120) {
    _scanlineBuffer->fillRect(0, 0 - offsetY, 480, 120, C_SKYBLUE);
  }

  // Horizon
  if (offsetY <= 120 && offsetY + SCANLINE_HEIGHT > 120) {
    _scanlineBuffer->drawLine(0, 120 - offsetY, 480, 120 - offsetY, 0xFFFF);
    _scanlineBuffer->drawLine(0, 121 - offsetY, 480, 121 - offsetY, 0xDEFB);
  }

  // Field perspective lines (getting closer together)
  int lines[] = {140, 165, 185, 202, 217, 230, 242, 253, 263, 272};
  for (int i = 0; i < 10; i++) {
    int lineY = lines[i];
    if (offsetY <= lineY && offsetY + SCANLINE_HEIGHT > lineY) {
      _scanlineBuffer->drawLine(0, lineY - offsetY, 480, lineY - offsetY,
                                0x2945);
    }
  }

  // Bottom line
  if (offsetY <= 280 && offsetY + SCANLINE_HEIGHT > 280) {
    _scanlineBuffer->drawLine(0, 280 - offsetY, 480, 280 - offsetY, C_WHITE);
  }

  // Penalty spot
  int spotY = KICK_SPOT_Y;
  if (spotY >= offsetY && spotY < offsetY + SCANLINE_HEIGHT) {
    _scanlineBuffer->fillCircle(KICK_SPOT_X, spotY - offsetY, 3, C_WHITE);
    _scanlineBuffer->drawCircle(KICK_SPOT_X, spotY - offsetY, 12, 0x4208);
  }
}

void GameEngine::drawGoal(int offsetY) {
  int left = GOAL_X - GOAL_WIDTH / 2;
  int right = GOAL_X + GOAL_WIDTH / 2;
  int top = GOAL_Y;
  int bottom = GOAL_Y + GOAL_HEIGHT;

  if (bottom < offsetY || top >= offsetY + SCANLINE_HEIGHT)
    return;

  // Dark background for depth
  _scanlineBuffer->fillRect(left - 15, top - offsetY, GOAL_WIDTH + 30,
                            GOAL_HEIGHT + 15, 0x18C3);

  // Posts (white with shadow)
  _scanlineBuffer->fillRect(left - 6, top - offsetY, 6, GOAL_HEIGHT, C_WHITE);
  _scanlineBuffer->fillRect(right, top - offsetY, 6, GOAL_HEIGHT, C_WHITE);
  _scanlineBuffer->fillRect(left - 6, top - 6 - offsetY, GOAL_WIDTH + 12, 6,
                            C_WHITE);

  // Net
  for (int x = left; x <= right; x += 10) {
    _scanlineBuffer->drawLine(x, top - offsetY, x, bottom - offsetY, 0xBDF7);
  }
  for (int y = top; y <= bottom; y += 10) {
    if (y >= offsetY && y < offsetY + SCANLINE_HEIGHT) {
      _scanlineBuffer->drawLine(left, y - offsetY, right, y - offsetY, 0xBDF7);
    }
  }

  // Shadow under crossbar
  _scanlineBuffer->drawLine(left, top + 6 - offsetY, right, top + 6 - offsetY,
                            0x2104);
  _scanlineBuffer->drawLine(left, top + 7 - offsetY, right, top + 7 - offsetY,
                            0x2104);
}

void GameEngine::drawKeeper(int offsetY) {
  int w = 35;
  int h = 35;
  int ky = _keeperPos.y;

  if (ky < offsetY || ky - h >= offsetY + SCANLINE_HEIGHT)
    return;

  // Body
  _scanlineBuffer->fillRect(_keeperPos.x - w / 2, ky - h - offsetY, w, h,
                            C_RED);

  // Head
  _scanlineBuffer->fillCircle(_keeperPos.x, ky - h - 8 - offsetY, 10, C_ORANGE);

  // Arms
  if (_keeperState == KEEPER_DIVE_LEFT) {
    _scanlineBuffer->fillRect(_keeperPos.x - w / 2 - 18, ky - h / 2 - offsetY,
                              18, 8, C_RED);
  } else if (_keeperState == KEEPER_DIVE_RIGHT) {
    _scanlineBuffer->fillRect(_keeperPos.x + w / 2, ky - h / 2 - offsetY, 18, 8,
                              C_RED);
  }
}

void GameEngine::drawPlayer(int offsetY) {
  int px = KICK_SPOT_X - 55;
  int py = KICK_SPOT_Y;

  if (py < offsetY || py - 50 >= offsetY + SCANLINE_HEIGHT)
    return;

  _scanlineBuffer->fillRect(px, py - 38 - offsetY, 38, 38, C_BLUE);
  _scanlineBuffer->fillCircle(px + 19, py - 50 - offsetY, 12, C_ORANGE);
  _scanlineBuffer->fillRect(px + 38, py - 22 - offsetY, 17, 10, C_BLUE);
}

void GameEngine::drawBall(int offsetY) {
  int r = 12 * _ball.scale;
  int by = _ball.pos.y;

  if (by < offsetY - r || by >= offsetY + SCANLINE_HEIGHT + r)
    return;

  _scanlineBuffer->fillCircle(_ball.pos.x + 3, by + 3 - offsetY, r,
                              C_DARKGREEN);
  _scanlineBuffer->fillCircle(_ball.pos.x, by - offsetY, r, C_WHITE);
  _scanlineBuffer->drawCircle(_ball.pos.x, by - offsetY, r, C_BLACK);

  if (r > 6) {
    _scanlineBuffer->fillCircle(_ball.pos.x - r / 3, by - r / 3 - offsetY,
                                r / 5, C_BLACK);
    _scanlineBuffer->fillCircle(_ball.pos.x + r / 3, by - r / 3 - offsetY,
                                r / 5, C_BLACK);
    _scanlineBuffer->fillCircle(_ball.pos.x, by + r / 3 - offsetY, r / 5,
                                C_BLACK);
  }
}

void GameEngine::drawCursor(int offsetY) {
  int x = _aimCursor.x;
  int y = _aimCursor.y;
  int s = 18;

  if (y < offsetY - s || y >= offsetY + SCANLINE_HEIGHT + s)
    return;

  _scanlineBuffer->drawLine(x - s, y - offsetY, x + s, y - offsetY, C_YELLOW);
  _scanlineBuffer->drawLine(x, y - s - offsetY, x, y + s - offsetY, C_YELLOW);
  _scanlineBuffer->drawCircle(x, y - offsetY, s, C_YELLOW);
  _scanlineBuffer->drawCircle(x, y - offsetY, s - 3, C_YELLOW);
  _scanlineBuffer->fillCircle(x, y - offsetY, 2, C_RED);
}

void GameEngine::drawPowerBar(int offsetY) {
  int w = 280;
  int h = 35;
  int x = (480 - w) / 2;
  int y = 130;

  if (y + h < offsetY || y >= offsetY + SCANLINE_HEIGHT)
    return;

  uint16_t barColor = C_GREEN;
  if (_powerLevel > 0.75f)
    barColor = C_RED;
  else if (_powerLevel > 0.5f)
    barColor = C_ORANGE;
  else if (_powerLevel > 0.25f)
    barColor = C_YELLOW;

  _scanlineBuffer->fillRect(x - 3, y - 3 - offsetY, w + 6, h + 6, C_BLACK);
  _scanlineBuffer->drawRect(x, y - offsetY, w, h, C_WHITE);
  _scanlineBuffer->drawRect(x + 1, y + 1 - offsetY, w - 2, h - 2, C_WHITE);
  _scanlineBuffer->fillRect(x + 3, y + 3 - offsetY, (w - 6) * _powerLevel,
                            h - 6, barColor);

  _scanlineBuffer->setTextColor(C_WHITE, C_BLACK);
  _scanlineBuffer->drawCentreString("POWER", 240, y + h + 8 - offsetY, 2);
}

void GameEngine::drawHUD(int offsetY) {
  int hudY = 295;

  if (hudY < offsetY || hudY >= offsetY + SCANLINE_HEIGHT)
    return;

  char buf[32];

  _scanlineBuffer->setTextColor(C_WHITE, C_GRASS);
  _scanlineBuffer->setTextSize(1);

  sprintf(buf, "Score:%d", _score);
  _scanlineBuffer->drawString(buf, 10, hudY - offsetY, 2);

  sprintf(buf, "Shot:%d/5", _shotsTaken + 1);
  _scanlineBuffer->drawString(buf, 190, hudY - offsetY, 2);

  sprintf(buf, "Goals:%d", _goalsScored);
  _scanlineBuffer->drawString(buf, 370, hudY - offsetY, 2);
}

void GameEngine::drawMenu(int offsetY) {
  int menuY = 90;
  int menuH = 140;

  if (menuY + menuH < offsetY || menuY >= offsetY + SCANLINE_HEIGHT)
    return;

  _scanlineBuffer->fillRect(90, menuY - offsetY, 300, menuH, C_BLACK);
  _scanlineBuffer->drawRect(90, menuY - offsetY, 300, menuH, C_YELLOW);
  _scanlineBuffer->drawRect(92, menuY + 2 - offsetY, 296, menuH - 4, C_YELLOW);

  _scanlineBuffer->setTextColor(C_YELLOW, C_BLACK);
  _scanlineBuffer->drawCentreString("PENALTY", 240, menuY + 20 - offsetY, 4);
  _scanlineBuffer->drawCentreString("SHOOTOUT", 240, menuY + 55 - offsetY, 4);

  _scanlineBuffer->setTextColor(C_WHITE, C_BLACK);
  _scanlineBuffer->drawCentreString("Press A to Start", 240,
                                    menuY + 100 - offsetY, 2);
}

void GameEngine::drawResultMsg(const char *msg, uint16_t color, int offsetY) {
  int msgY = 150;

  if (msgY < offsetY || msgY + 50 >= offsetY + SCANLINE_HEIGHT)
    return;

  _scanlineBuffer->setTextColor(color, C_GRASS);
  _scanlineBuffer->drawCentreString(msg, 240, msgY - offsetY, 4);

  if (_state == STATE_GOAL) {
    char buf[32];
    int shotScore = 100 + (int)(_powerLevel * 100);
    sprintf(buf, "+%d points!", shotScore);
    _scanlineBuffer->setTextColor(C_YELLOW, C_GRASS);
    _scanlineBuffer->drawCentreString(buf, 240, msgY + 35 - offsetY, 2);
  }
}

void GameEngine::drawGameOver(int offsetY) {
  int goY = 70;
  int goH = 180;

  if (goY + goH < offsetY || goY >= offsetY + SCANLINE_HEIGHT)
    return;

  char buf[64];

  _scanlineBuffer->fillRect(70, goY - offsetY, 340, goH, C_BLACK);
  _scanlineBuffer->drawRect(70, goY - offsetY, 340, goH, C_YELLOW);
  _scanlineBuffer->drawRect(72, goY + 2 - offsetY, 336, goH - 4, C_YELLOW);

  _scanlineBuffer->setTextColor(C_YELLOW, C_BLACK);
  _scanlineBuffer->drawCentreString("GAME OVER", 240, goY + 15 - offsetY, 4);

  _scanlineBuffer->setTextColor(C_WHITE, C_BLACK);

  sprintf(buf, "Score: %d", _score);
  _scanlineBuffer->drawCentreString(buf, 240, goY + 60 - offsetY, 4);

  sprintf(buf, "Goals: %d / 5", _goalsScored);
  _scanlineBuffer->drawCentreString(buf, 240, goY + 100 - offsetY, 2);

  if (_score > _highScore) {
    _highScore = _score;
    _scanlineBuffer->setTextColor(C_GREEN, C_BLACK);
    _scanlineBuffer->drawCentreString("NEW HIGH SCORE!", 240,
                                      goY + 125 - offsetY, 2);
  }

  _scanlineBuffer->setTextColor(C_WHITE, C_BLACK);
  _scanlineBuffer->drawCentreString("Press A to Restart", 240,
                                    goY + 150 - offsetY, 2);
}

void GameEngine::drawInstructions(const char *text, int offsetY) {
  int instY = 105;

  if (instY < offsetY || instY + 20 >= offsetY + SCANLINE_HEIGHT)
    return;

  _scanlineBuffer->fillRect(0, instY - offsetY, 480, 22, C_BLACK);
  _scanlineBuffer->setTextColor(C_YELLOW, C_BLACK);
  _scanlineBuffer->drawCentreString(text, 240, instY + 3 - offsetY, 2);
}
