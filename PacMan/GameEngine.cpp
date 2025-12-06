#include "GameEngine.h"
#include "Assets.h"
#include "System.h"

Preferences preferences;

#define SCREEN_W 480
#define SCREEN_H 320
#define TILE_SIZE 24
// Shift maze to the left to leave space for HUD on the right
#define MAZE_OFFSET_X 10
#define MAZE_OFFSET_Y 4

GameEngine::GameEngine(TFT_eSPI *tft, Input *input) : _tft(tft), _input(input) {
  _canvas = new TFT_eSprite(tft);
  _state = STATE_MENU;
  _score = 0;
  _highScore = 0;
  _lives = 3;
  _level = 1;
  _dotsEaten = 0;
  _coins = 0;
  _totalCoins = 0;

  _pacman = {8, 9};
  _prevPacman = _pacman;
  _pacmanDir = DIR_RIGHT;
  _nextDir = DIR_NONE;
  _animFrame = 0;
  _mouthOpen = true;
  _moveTimer = 0;
  _ghostMoveTimer = 0;

  _frightenedMode = false;
  _frightenedTime = 0;

  _clickDebounce = false;
  _lastClickTime = 0;

  _touchStartX = 0;
  _touchStartY = 0;
  _isTouching = false;
  _lastJoystickDir = -1;
  _lastJoystickTime = 0;
  _lastTouchY = 0;
  _touchJustStarted = false;

  _selectedSkin = 0;
  _selectedTheme = 0;
  _ownedSkins[0] = true;
  _ownedThemes[0] = true;
  _shopScrollOffset = 0;

  _selectedMenuItem = 0;
  _selectedShopItem = 0;
  _selectedPauseOption = 0;
  _lastJoystickMenuTime = 0;
  _joystickMenuMoved = false;

  for (int i = 1; i < 8; i++) {
    _ownedSkins[i] = false;
    if (i < 4)
      _ownedThemes[i] = false;
  }
}

void GameEngine::init() {
  preferences.begin("pacman", false);
  _highScore = preferences.getInt("highScore", 0);
  _totalCoins = preferences.getInt("totalCoins", 0);
  _selectedSkin = preferences.getInt("skin", 0);
  _selectedTheme = preferences.getInt("theme", 0);

  for (int i = 0; i < 8; i++) {
    _ownedSkins[i] = preferences.getBool(("skin_" + String(i)).c_str(), i == 0);
    if (i < 4)
      _ownedThemes[i] =
          preferences.getBool(("theme_" + String(i)).c_str(), i == 0);
  }

  _canvas->setColorDepth(16);
  void *ptr = _canvas->createSprite(SCREEN_W, 32);
  if (!ptr) {
    Serial.println("Failed to create strip sprite!");
    _useSprite = false;
  } else {
    Serial.println("Strip sprite created successfully!");
    _useSprite = true;
  }

  loadMaze(_level);
  initializeGhosts();
}

void GameEngine::loadMaze() { loadMaze(_level); }

void GameEngine::loadMaze(int level) {
  // New 17x13 Maze
  // 0=Dot, 1=Wall, 2=PowerPellet, 3=Empty, 4=Cage(No Pacman)
  int mazeTemplate[13][17] = {
      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
      {1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1},
      {1, 2, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 2, 1},
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
      {1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1},
      {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
      {1, 1, 1, 1, 0, 1, 4, 4, 4, 1, 0, 1, 1, 1, 1, 1,
       1}, // Row 6: Cage at 7,8,9
      {1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 1},
      {1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1},
      {1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1},
      {1, 2, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 2, 1},
      {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}};

  _dots.clear();
  _powerPellets.clear();

  for (int y = 0; y < MAZE_HEIGHT; y++) {
    for (int x = 0; x < MAZE_WIDTH; x++) {
      _maze[y][x] = mazeTemplate[y][x];
      if (_maze[y][x] == 0)
        _dots.push_back({x, y});
      else if (_maze[y][x] == 2)
        _powerPellets.push_back({x, y});
    }
  }
}

float GameEngine::getPacmanSpeed() {
  return 0.20f; // Adjusted for 24px tiles
}

float GameEngine::getGhostSpeed() { return 0.25f; }

void GameEngine::updatePacman(float dt) {
  _moveTimer += dt;
  if (_moveTimer >= getPacmanSpeed()) {
    _moveTimer = 0;
    _prevPacman = _pacman;
    if (_nextDir != DIR_NONE && _nextDir != _pacmanDir) {
      if (isValidMove(_pacman.x, _pacman.y, _nextDir))
        _pacmanDir = _nextDir;
    }
    movePacman();
  }
}

void GameEngine::updateGhosts(float dt) {
  _ghostMoveTimer += dt;
  if (_ghostMoveTimer >= getGhostSpeed()) {
    _ghostMoveTimer = 0;
    for (auto &ghost : _ghosts) {
      ghost.prevPos = ghost.pos;
      updateGhost(ghost, dt);
    }
  }
}

void GameEngine::initializeGhosts() {
  _ghosts.clear();
  // Adjust spawn points for 17x13 maze
  // Cage is at 7,6 / 8,6 / 9,6
  // Blinky outside at 8,5
  // Others inside
  Ghost blinky = {{8, 5}, {8, 5}, {0, 0}, DIR_LEFT, 0, false, false, 0};
  Ghost pinky = {{7, 6}, {7, 6}, {0, 0}, DIR_UP, 1, false, false, 0};
  Ghost inky = {{8, 6}, {8, 6}, {0, 0}, DIR_UP, 2, false, false, 0};
  Ghost clyde = {{9, 6}, {9, 6}, {0, 0}, DIR_UP, 3, false, false, 0};
  _ghosts.push_back(blinky);
  _ghosts.push_back(pinky);
  _ghosts.push_back(inky);
  _ghosts.push_back(clyde);
}

void GameEngine::startGame() {
  _state = STATE_PLAYING;
  _score = 0;
  _lives = 3;
  _level = 1;
  _dotsEaten = 0;
  resetLevel();
  _gameStartTime = millis();
}

void GameEngine::resetLevel() {
  _pacman = {8, 9}; // Center bottomish
  _prevPacman = _pacman;
  _pacmanDir = DIR_RIGHT;
  _nextDir = DIR_NONE;
  _animFrame = 0;
  _mouthOpen = true;
  _moveTimer = 0;
  _frightenedMode = false;
  _frightenedTime = 0;
  initializeGhosts();
  loadMaze();
}

void GameEngine::update(float dt) {
  Point touch = _input->getTouch(SCREEN_W, SCREEN_H);
  unsigned long currentTime = millis();

  static unsigned long lastTouchTime = 0;
  if (touch.touched)
    lastTouchTime = currentTime;
  else if (currentTime - lastTouchTime > 100)
    _clickDebounce = false;

  if (_clickDebounce)
    touch.touched = false;

  switch (_state) {
  case STATE_MENU: {
    ButtonInput buttons = _input->getButtons();
    JoystickInput joy = _input->getJoystick();
    if (joy.active && !_joystickMenuMoved) {
      if (joy.direction == INPUT_DIR_DOWN && _selectedMenuItem < 2) {
        _selectedMenuItem++;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      } else if (joy.direction == INPUT_DIR_UP && _selectedMenuItem > 0) {
        _selectedMenuItem--;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      }
    }
    if (!joy.active || currentTime - _lastJoystickMenuTime > 200)
      _joystickMenuMoved = false;

    if (buttons.aJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      if (_selectedMenuItem == 0)
        startGame();
      else if (_selectedMenuItem == 1)
        _state = STATE_SHOP;
      else if (_selectedMenuItem == 2)
        returnToMenu();
    }

    if (touch.touched && !_clickDebounce) {
      if (touch.y >= 200 && touch.y <= 250 && touch.x >= 120 &&
          touch.x <= 360) {
        _clickDebounce = true;
        _selectedMenuItem = 0;
        startGame();
      } else if (touch.y >= 260 && touch.y <= 310 && touch.x >= 120 &&
                 touch.x <= 360) {
        _clickDebounce = true;
        _selectedMenuItem = 1;
        _state = STATE_SHOP;
      } else if (touch.x >= SCREEN_W - 80 && touch.x <= SCREEN_W - 20 &&
                 touch.y >= 20 && touch.y <= 60) {
        _clickDebounce = true;
        _selectedMenuItem = 2;
        returnToMenu();
      }
    }
    break;
  }
  case STATE_SHOP: {
    ButtonInput buttons = _input->getButtons();
    JoystickInput joy = _input->getJoystick();
    if (joy.active && !_joystickMenuMoved) {
      if (joy.direction == INPUT_DIR_RIGHT && (_selectedShopItem % 4) < 3 &&
          _selectedShopItem < 11) {
        _selectedShopItem++;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      } else if (joy.direction == INPUT_DIR_LEFT &&
                 (_selectedShopItem % 4) > 0) {
        _selectedShopItem--;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      } else if (joy.direction == INPUT_DIR_DOWN) {
        if (_selectedShopItem < 8)
          _selectedShopItem += 4;
        else if (_selectedShopItem < 12)
          _selectedShopItem = 12; // Go to Back button
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      } else if (joy.direction == INPUT_DIR_UP) {
        if (_selectedShopItem == 12)
          _selectedShopItem = 8;
        else if (_selectedShopItem >= 4)
          _selectedShopItem -= 4;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      }
    }
    if (!joy.active || currentTime - _lastJoystickMenuTime > 200)
      _joystickMenuMoved = false;

    // Manual scroll logic only (removed auto-scroll)
    // Ensure selected item is visible
    int row = _selectedShopItem / 4;
    int itemY = 120 + row * 105;
    if (itemY - _shopScrollOffset > 200)
      _shopScrollOffset = itemY - 200;
    if (itemY - _shopScrollOffset < 50)
      _shopScrollOffset = itemY - 50;
    if (_shopScrollOffset < 0)
      _shopScrollOffset = 0;
    if (_shopScrollOffset > 200)
      _shopScrollOffset = 200;

    if (buttons.aJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      if (_selectedShopItem == 12) {
        _state = STATE_MENU;
        break;
      }
      bool isSkin = (_selectedShopItem < 8);
      int itemIndex = isSkin ? _selectedShopItem : (_selectedShopItem - 8);
      int price = isSkin ? 50 : 100;
      if (isSkin) {
        if (_ownedSkins[itemIndex]) {
          _selectedSkin = itemIndex;
          preferences.putInt("skin", itemIndex);
        } else if (_totalCoins >= price) {
          _totalCoins -= price;
          _ownedSkins[itemIndex] = true;
          _selectedSkin = itemIndex;
          preferences.putBool(("skin_" + String(itemIndex)).c_str(), true);
          preferences.putInt("skin", itemIndex);
          preferences.putInt("totalCoins", _totalCoins);
        }
      } else {
        if (_ownedThemes[itemIndex]) {
          _selectedTheme = itemIndex;
          preferences.putInt("theme", itemIndex);
        } else if (_totalCoins >= price) {
          _totalCoins -= price;
          _ownedThemes[itemIndex] = true;
          _selectedTheme = itemIndex;
          preferences.putBool(("theme_" + String(itemIndex)).c_str(), true);
          preferences.putInt("theme", itemIndex);
          preferences.putInt("totalCoins", _totalCoins);
        }
      }
    }
    if (buttons.bJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      _state = STATE_MENU;
    }

    // Touch Logic with Scroll
    if (touch.touched) {
      if (!_touchJustStarted) {
        _lastTouchY = touch.y;
        _touchStartY = touch.y; // Store initial Y for drag detection
        _touchJustStarted = true;
      } else {
        int dy = touch.y - _lastTouchY;
        _shopScrollOffset -= dy;
        if (_shopScrollOffset < 0)
          _shopScrollOffset = 0;
        if (_shopScrollOffset > 200)
          _shopScrollOffset = 200; // Max scroll
        _lastTouchY = touch.y;
      }

      // Calculate total drag distance from initial touch
      int totalDrag = abs(touch.y - _touchStartY);

      // Only process clicks if drag is minimal (less than 10 pixels)
      if (!_clickDebounce && totalDrag < 10) {
        // Back Button (Fixed at top)
        if (touch.y >= 70 && touch.y <= 105 && touch.x >= SCREEN_W / 2 - 60 &&
            touch.x <= SCREEN_W / 2 + 60) {
          _state = STATE_MENU;
          _clickDebounce = true;
        }

        // Shop Items
        int boxWidth = 100;
        int boxHeight = 90;
        int spacing = 15;
        int startX = 20;
        int startY = 120;
        int touchY = touch.y + _shopScrollOffset; // Adjust touch for scroll

        for (int i = 0; i < 12; i++) {
          int col = i % 4;
          int row = i / 4;
          int boxX = startX + col * (boxWidth + spacing);
          int boxY = startY + row * (boxHeight + spacing);
          if (touch.x >= boxX && touch.x < boxX + boxWidth && touchY >= boxY &&
              touchY < boxY + boxHeight) {
            _clickDebounce = true;
            _selectedShopItem = i;
            // Trigger purchase/select logic
            bool isSkin = (_selectedShopItem < 8);
            int itemIndex =
                isSkin ? _selectedShopItem : (_selectedShopItem - 8);
            int price = isSkin ? 50 : 100;
            if (isSkin) {
              if (_ownedSkins[itemIndex]) {
                _selectedSkin = itemIndex;
                preferences.putInt("skin", itemIndex);
              } else if (_totalCoins >= price) {
                _totalCoins -= price;
                _ownedSkins[itemIndex] = true;
                _selectedSkin = itemIndex;
                preferences.putBool(("skin_" + String(itemIndex)).c_str(),
                                    true);
                preferences.putInt("skin", itemIndex);
                preferences.putInt("totalCoins", _totalCoins);
              }
            } else {
              if (_ownedThemes[itemIndex]) {
                _selectedTheme = itemIndex;
                preferences.putInt("theme", itemIndex);
              } else if (_totalCoins >= price) {
                _totalCoins -= price;
                _ownedThemes[itemIndex] = true;
                _selectedTheme = itemIndex;
                preferences.putBool(("theme_" + String(itemIndex)).c_str(),
                                    true);
                preferences.putInt("theme", itemIndex);
                preferences.putInt("totalCoins", _totalCoins);
              }
            }
          }
        }
      }
    } else {
      _touchJustStarted = false;
    }
    break;
  }
  case STATE_PLAYING: {
    ButtonInput buttons = _input->getButtons();
    if (buttons.bJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      _selectedPauseOption = 0;
      _state = STATE_PAUSED;
      return;
    }
    if (touch.touched && !_clickDebounce) {
      // Pause Button Touch (Bottom Right)
      if (touch.x >= 425 && touch.y >= 250) {
        _clickDebounce = true;
        _selectedPauseOption = 0;
        _state = STATE_PAUSED;
        return;
      }
    }
    handleInput(touch);
    updatePacman(dt);
    updateGhosts(dt);
    checkCollisions();
    _animFrame += dt * 5;
    if (_animFrame >= 2) {
      _animFrame = 0;
      _mouthOpen = !_mouthOpen;
    }
    if (_frightenedMode && millis() - _frightenedStart > _frightenedTime) {
      _frightenedMode = false;
      for (auto &ghost : _ghosts)
        ghost.frightened = false;
    }
    if (_dots.empty() && _powerPellets.empty())
      nextLevel();
    break;
  }
  case STATE_PAUSED: {
    ButtonInput buttons = _input->getButtons();
    JoystickInput joy = _input->getJoystick();
    if (joy.active && !_joystickMenuMoved) {
      if (joy.direction == INPUT_DIR_DOWN && _selectedPauseOption < 1) {
        _selectedPauseOption++;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      } else if (joy.direction == INPUT_DIR_UP && _selectedPauseOption > 0) {
        _selectedPauseOption--;
        _joystickMenuMoved = true;
        _lastJoystickMenuTime = currentTime;
      }
    }
    if (!joy.active || currentTime - _lastJoystickMenuTime > 200)
      _joystickMenuMoved = false;
    if (buttons.aJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      if (_selectedPauseOption == 0)
        _state = STATE_PLAYING;
      else
        _state = STATE_MENU;
    }
    if (buttons.bJustPressed && !_clickDebounce) {
      _clickDebounce = true;
      _selectedPauseOption = 0;
      _state = STATE_PLAYING;
    }
    if (touch.touched && !_clickDebounce) {
      int panelWidth = 280;
      int panelX = (SCREEN_W - panelWidth) / 2;
      if (touch.y >= 150 && touch.y <= 185 && touch.x >= panelX + 40 &&
          touch.x <= panelX + panelWidth - 40) {
        _clickDebounce = true;
        _selectedPauseOption = 0;
        _state = STATE_PLAYING;
      } else if (touch.y >= 200 && touch.y <= 235 && touch.x >= panelX + 40 &&
                 touch.x <= panelX + panelWidth - 40) {
        _clickDebounce = true;
        _selectedPauseOption = 1;
        _state = STATE_MENU;
      }
    }
    break;
  }
  case STATE_GAMEOVER:
  case STATE_WIN:
    if (touch.touched && !_clickDebounce) {
      if (touch.y > 220 && touch.y < 270 && touch.x > 120 && touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_MENU;
      }
    }
    break;
  }
}

void GameEngine::handleInput(Point touch) {
  JoystickInput joy = _input->getJoystick();
  if (joy.active && joy.direction != INPUT_DIR_NONE) {
    unsigned long currentTime = millis();
    Direction gameDir = DIR_NONE;
    switch (joy.direction) {
    case INPUT_DIR_UP:
      gameDir = DIR_UP;
      break;
    case INPUT_DIR_DOWN:
      gameDir = DIR_DOWN;
      break;
    case INPUT_DIR_LEFT:
      gameDir = DIR_LEFT;
      break;
    case INPUT_DIR_RIGHT:
      gameDir = DIR_RIGHT;
      break;
    default:
      gameDir = DIR_NONE;
      break;
    }
    if (_lastJoystickDir != gameDir || currentTime - _lastJoystickTime > 150) {
      _nextDir = gameDir;
      _lastJoystickDir = gameDir;
      _lastJoystickTime = currentTime;
    }
  } else {
    _lastJoystickDir = -1;
  }
  if (touch.touched) {
    bool isPauseArea =
        touch.x > SCREEN_W - 60 && touch.y > 250; // Updated pause area check
    if (!_isTouching && !isPauseArea) {
      _touchStartX = touch.x;
      _touchStartY = touch.y;
      _isTouching = true;
    } else if (_isTouching && !isPauseArea) {
      int dx = touch.x - _touchStartX;
      int dy = touch.y - _touchStartY;
      if (abs(dx) > 15 || abs(dy) > 15) {
        if (abs(dx) > abs(dy))
          _nextDir = (dx > 0) ? DIR_RIGHT : DIR_LEFT;
        else
          _nextDir = (dy > 0) ? DIR_DOWN : DIR_UP;
        _touchStartX = touch.x;
        _touchStartY = touch.y;
      }
    }
  } else {
    _isTouching = false;
  }
}

void GameEngine::movePacman() {
  if (isValidMove(_pacman.x, _pacman.y, _pacmanDir)) {
    _pacman = getNextPosition(_pacman, _pacmanDir);
    eatDot(_pacman.x, _pacman.y);
    for (auto it = _powerPellets.begin(); it != _powerPellets.end();) {
      if (it->x == _pacman.x && it->y == _pacman.y) {
        eatPowerPellet(it->x, it->y);
        it = _powerPellets.erase(it);
      } else
        ++it;
    }
  }
}

void GameEngine::updateGhost(Ghost &ghost, float dt) {
  if (ghost.eaten) {
    if (millis() - ghost.deadTime > 8000) {
      ghost.eaten = false;
      ghost.frightened = false;
      ghost.pos = {8, 6};    // Respawn in box
      ghost.target = {8, 4}; // Target exit
    }
    return; // Don't move while dead/hidden
  }

  if (ghost.pos.y == 6 && ghost.pos.x >= 7 && ghost.pos.x <= 9) {
    ghost.target = {8, 4};
  } else if (_frightenedMode) {
    ghost.target = {random(0, MAZE_WIDTH), random(0, MAZE_HEIGHT)};
  } else {
    ghost.target = getGhostTarget(ghost.type);
  }
  std::vector<Direction> possibleDirs;
  for (int dir = 0; dir < 4; dir++) {
    if (isValidMove(ghost.pos.x, ghost.pos.y, (Direction)dir) &&
        dir != getOppositeDirection(ghost.dir)) {
      possibleDirs.push_back((Direction)dir);
    }
  }
  // If no valid moves (dead end), allow reversing
  if (possibleDirs.empty()) {
    if (isValidMove(ghost.pos.x, ghost.pos.y,
                    getOppositeDirection(ghost.dir))) {
      possibleDirs.push_back(getOppositeDirection(ghost.dir));
    }
  }

  if (!possibleDirs.empty()) {
    Direction bestDir = possibleDirs[0];
    int bestDist =
        manhattanDistance(getNextPosition(ghost.pos, bestDir), ghost.target);
    for (auto dir : possibleDirs) {
      int dist =
          manhattanDistance(getNextPosition(ghost.pos, dir), ghost.target);
      if (dist < bestDist) {
        bestDist = dist;
        bestDir = dir;
      }
    }
    ghost.dir = bestDir;
    ghost.pos = getNextPosition(ghost.pos, ghost.dir);
  }
}

Position GameEngine::getGhostTarget(int ghostType) {
  Position target = _pacman;
  switch (ghostType) {
  case 0: // Blinky - Directly chases Pacman
    target = _pacman;
    break;
  case 1: // Pinky - Targets 4 tiles ahead
    target = getNextPosition(getNextPosition(_pacman, _pacmanDir), _pacmanDir);
    target = getNextPosition(getNextPosition(target, _pacmanDir), _pacmanDir);
    break;
  case 2: // Inky - Aggressive intercept
    target = getNextPosition(_pacman, _pacmanDir);
    break;
  case 3: // Clyde - Random or Chase
    if (manhattanDistance(_ghosts[3].pos, _pacman) < 8)
      target = {0, MAZE_HEIGHT - 1};
    else
      target = _pacman;
    break;
  }
  return target;
}

void GameEngine::checkCollisions() {
  for (auto &ghost : _ghosts) {
    if (ghost.eaten)
      continue; // Skip collision if ghost is dead

    // Standard collision check
    bool collision = (ghost.pos.x == _pacman.x && ghost.pos.y == _pacman.y);

    // Swap collision check (moving into each other)
    if (!collision && ghost.pos.x == _prevPacman.x &&
        ghost.pos.y == _prevPacman.y && ghost.prevPos.x == _pacman.x &&
        ghost.prevPos.y == _pacman.y) {
      collision = true;
    }

    if (collision) {
      if (ghost.frightened) {
        ghost.eaten = true;
        ghost.deadTime = millis(); // Set death time
        ghost.frightened = false;
        _score += 200;
      } else {
        _lives--;
        if (_lives <= 0)
          gameOver();
        else {
          respawnPacman();
          respawnGhosts();
        }
      }
    }
  }
}

void GameEngine::eatDot(int x, int y) {
  for (auto it = _dots.begin(); it != _dots.end();) {
    if (it->x == x && it->y == y) {
      _score += 10;
      _coins += 1;
      _totalCoins += 1;
      _dotsEaten++;
      _maze[y][x] = 3;
      it = _dots.erase(it);
      preferences.putInt("totalCoins", _totalCoins);
      break;
    } else
      ++it;
  }
}

void GameEngine::eatPowerPellet(int x, int y) {
  _score += 50;
  _coins += 5;
  _totalCoins += 5;
  _maze[y][x] = 3; // Mark as eaten (empty)
  scareGhosts();
  preferences.putInt("totalCoins", _totalCoins);
}

void GameEngine::scareGhosts() {
  _frightenedMode = true;
  _frightenedStart = millis();
  _frightenedTime = 8000; // Increased to 8s
  for (auto &ghost : _ghosts) {
    if (!ghost.eaten) {
      ghost.frightened = true;
      ghost.dir = getOppositeDirection(ghost.dir);
    }
  }
}

void GameEngine::respawnPacman() {
  _pacman = {8, 9};
  _pacmanDir = DIR_RIGHT;
  _nextDir = DIR_NONE;
  _moveTimer = 0;
}

void GameEngine::respawnGhosts() { initializeGhosts(); }

void GameEngine::nextLevel() {
  _level++;
  resetLevel();
  if (_level > 5) {
    _state = STATE_WIN;
    if (_score > _highScore) {
      _highScore = _score;
      preferences.putInt("highScore", _highScore);
    }
  }
}

void GameEngine::gameOver() {
  _state = STATE_GAMEOVER;
  if (_score > _highScore) {
    _highScore = _score;
    preferences.putInt("highScore", _highScore);
  }
}

void GameEngine::draw() {
  if (!_useSprite) {
    _tft->fillScreen(TFT_BLACK);
    return;
  }
  for (int y = 0; y < SCREEN_H; y += 32) {
    _canvas->fillSprite(TFT_BLACK);
    switch (_state) {
    case STATE_MENU:
      drawMenu(y);
      break;
    case STATE_PLAYING:
      drawMaze(y);
      drawPacman(y);
      drawGhosts(y);
      drawHUD(y);
      break;
    case STATE_PAUSED:
      drawMaze(y);
      drawPacman(y);
      drawGhosts(y);
      drawHUD(y);
      drawPauseMenu(y);
      break;
    case STATE_GAMEOVER:
      drawGameOver(y);
      break;
    case STATE_WIN:
      drawWinScreen(y);
      break;
    case STATE_SHOP:
      drawShop(y);
      break;
    }
    _canvas->pushSprite(0, y);
  }
}

void GameEngine::drawMaze(int offsetY) {
  uint16_t wallColor, wallInnerColor;
  switch (_selectedTheme) {
  case 1:
    wallColor = 0xD820;
    wallInnerColor = 0xB800;
    break;
  case 2:
    wallColor = 0x3540;
    wallInnerColor = 0x2500;
    break;
  case 3:
    wallColor = 0xFD20;
    wallInnerColor = 0xFB00;
    break;
  default:
    wallColor = C_BLUE;
    wallInnerColor = 0x0010;
    break;
  }
  for (int y = 0; y < MAZE_HEIGHT; y++) {
    for (int x = 0; x < MAZE_WIDTH; x++) {
      int screenX = MAZE_OFFSET_X + x * TILE_SIZE;
      int screenY = MAZE_OFFSET_Y + y * TILE_SIZE - offsetY;
      if (screenY < -TILE_SIZE || screenY >= 32)
        continue;
      switch (_maze[y][x]) {
      case 1: // Wall
        _canvas->fillRect(screenX, screenY, TILE_SIZE, TILE_SIZE,
                          wallInnerColor);
        _canvas->drawRect(screenX, screenY, TILE_SIZE, TILE_SIZE, wallColor);
        break;
      case 0:
        _canvas->fillCircle(screenX + TILE_SIZE / 2, screenY + TILE_SIZE / 2, 2,
                            C_WHIT);
        break;
      case 2: {
        int pulse = (millis() / 150) % 2;
        int pelletSize = 4 + pulse;
        _canvas->fillCircle(screenX + TILE_SIZE / 2, screenY + TILE_SIZE / 2,
                            pelletSize, C_WHIT);
      } break;
      case 4: // Cage (Draw as empty but maybe a door line?)
        // Optional: Draw a door line for the cage entrance if needed, but for
        // now just empty
        break;
      }
    }
  }
}

void GameEngine::drawPacman(int offsetY) {
  float t = _moveTimer / getPacmanSpeed();
  if (t > 1.0f)
    t = 1.0f;
  float interpX = _prevPacman.x + (_pacman.x - _prevPacman.x) * t;
  float interpY = _prevPacman.y + (_pacman.y - _prevPacman.y) * t;
  int screenX = MAZE_OFFSET_X + (int)(interpX * TILE_SIZE);
  int screenY = MAZE_OFFSET_Y + (int)(interpY * TILE_SIZE) - offsetY;
  if (screenY < -TILE_SIZE || screenY >= 32)
    return;
  int radius = TILE_SIZE / 2 - 1;
  int centerX = screenX + TILE_SIZE / 2;
  int centerY = screenY + TILE_SIZE / 2;
  uint16_t pacmanColor = C_YELL;
  if (_selectedSkin == 1)
    pacmanColor = C_PINK;
  else if (_selectedSkin == 2)
    pacmanColor = C_CYAN;
  else if (_selectedSkin == 3)
    pacmanColor = C_GREN;
  else if (_selectedSkin == 4)
    pacmanColor = 0xF800; // Red
  else if (_selectedSkin == 5)
    pacmanColor = 0x7E0; // Green
  else if (_selectedSkin == 6)
    pacmanColor = 0x001F; // Blue
  else if (_selectedSkin == 7)
    pacmanColor = 0xFFFF; // White

  _canvas->fillCircle(centerX, centerY, radius, pacmanColor);

  if (_mouthOpen) {
    int x1 = centerX, y1 = centerY;
    int x2 = centerX, y2 = centerY;
    int x3 = centerX, y3 = centerY;
    switch (_pacmanDir) {
    case DIR_RIGHT:
      x1 = centerX;
      y1 = centerY;
      x2 = centerX + radius;
      y2 = centerY - radius / 2;
      x3 = centerX + radius;
      y3 = centerY + radius / 2;
      break;
    case DIR_LEFT:
      x1 = centerX;
      y1 = centerY;
      x2 = centerX - radius;
      y2 = centerY - radius / 2;
      x3 = centerX - radius;
      y3 = centerY + radius / 2;
      break;
    case DIR_UP:
      x1 = centerX;
      y1 = centerY;
      x2 = centerX - radius / 2;
      y2 = centerY - radius;
      x3 = centerX + radius / 2;
      y3 = centerY - radius;
      break;
    case DIR_DOWN:
      x1 = centerX;
      y1 = centerY;
      x2 = centerX - radius / 2;
      y2 = centerY + radius;
      x3 = centerX + radius / 2;
      y3 = centerY + radius;
      break;
    }
    _canvas->fillTriangle(x1, y1, x2, y2, x3, y3, TFT_BLACK);
  }
  if (!_mouthOpen) {
    int eyeX = centerX;
    int eyeY = centerY - 5;
    if (_pacmanDir == DIR_UP)
      eyeY = centerY + 2;
    if (_pacmanDir == DIR_DOWN)
      eyeY = centerY - 2;
    if (_pacmanDir == DIR_LEFT)
      eyeX = centerX - 2;
    if (_pacmanDir == DIR_RIGHT)
      eyeX = centerX + 2;
    _canvas->fillCircle(eyeX, eyeY, 2, TFT_BLACK);
  }
}

void GameEngine::drawGhosts(int offsetY) {
  for (const auto &ghost : _ghosts)
    drawGhost(ghost, offsetY);
}

void GameEngine::drawGhost(const Ghost &ghost, int offsetY) {
  if (ghost.eaten)
    return; // Don't draw if eaten/hidden

  float t = _ghostMoveTimer / getGhostSpeed();
  if (t > 1.0f)
    t = 1.0f;
  float interpX = ghost.prevPos.x + (ghost.pos.x - ghost.prevPos.x) * t;
  float interpY = ghost.prevPos.y + (ghost.pos.y - ghost.prevPos.y) * t;
  int screenX = MAZE_OFFSET_X + (int)(interpX * TILE_SIZE);
  int screenY = MAZE_OFFSET_Y + (int)(interpY * TILE_SIZE) - offsetY;
  if (screenY < -TILE_SIZE || screenY >= 32)
    return;
  uint16_t color = C_RED;
  if (ghost.frightened)
    color = (millis() / 250) % 2 ? C_BLUE : C_WHIT;
  else {
    switch (ghost.type) {
    case 0:
      color = C_RED;
      break;
    case 1:
      color = C_PINK;
      break;
    case 2:
      color = C_CYAN;
      break;
    case 3:
      color = C_ORNG;
      break;
    }
  }
  int centerX = screenX + TILE_SIZE / 2;
  int centerY = screenY + TILE_SIZE / 2;
  int radius = TILE_SIZE / 2 - 1;
  _canvas->fillCircle(centerX, centerY - 2, radius, color);
  _canvas->fillRect(screenX + 2, centerY - 2, TILE_SIZE - 4, radius + 2, color);
  for (int i = 0; i < 3; i++) {
    int footX = screenX + 2 + i * 7;
    _canvas->fillTriangle(footX, screenY + TILE_SIZE - 2, footX + 3,
                          screenY + TILE_SIZE + 2, footX + 6,
                          screenY + TILE_SIZE - 2, color);
  }

  _canvas->fillCircle(centerX - 4, centerY - 2, 4, C_WHIT);
  _canvas->fillCircle(centerX + 4, centerY - 2, 4, C_WHIT);
  int pupX = 0, pupY = 0;
  if (ghost.dir == DIR_LEFT)
    pupX = -2;
  if (ghost.dir == DIR_RIGHT)
    pupX = 2;
  if (ghost.dir == DIR_UP)
    pupY = -2;
  if (ghost.dir == DIR_DOWN)
    pupY = 2;
  _canvas->fillCircle(centerX - 4 + pupX, centerY - 2 + pupY, 2, C_BLUE);
  _canvas->fillCircle(centerX + 4 + pupX, centerY - 2 + pupY, 2, C_BLUE);
}

void GameEngine::drawHUD(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(TL_DATUM);

  // Draw HUD on the right side (x > 418)
  int hudX = 425;

  // Helper lambda to draw if visible in current strip
  auto drawIfVisible = [&](int y, int h, std::function<void(int)> drawFn) {
    if (y + h > offsetY && y < offsetY + 32) {
      drawFn(y - offsetY);
    }
  };

  // Score Label (Y=40, H=20)
  drawIfVisible(40, 20, [&](int localY) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("SCORE", hudX, localY);
  });

  // Score Value (Y=60, H=20)
  drawIfVisible(60, 20, [&](int localY) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString(String(_score), hudX, localY);
  });

  // Lives Label (Y=100, H=20)
  drawIfVisible(100, 20, [&](int localY) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("LIVES", hudX, localY);
  });

  // Lives Icons (Y=130, H=60 for 3 lives) - Shifted down to 130
  drawIfVisible(130, 60, [&](int localY) {
    for (int i = 0; i < _lives; i++) {
      int iconY = 135 + (i * 20);
      if (iconY > offsetY && iconY < offsetY + 32) {
        _canvas->fillCircle(hudX + 8, iconY - offsetY, 6, C_YELL);
      }
    }
  });

  // Pause Button (Y=250, H=40)
  drawIfVisible(250, 40, [&](int localY) {
    int btnX = 450; // Centered in HUD area (420 to 480)
    _canvas->fillCircle(btnX, localY + 10, 18, C_BLUE);
    _canvas->fillRect(btnX - 4, localY + 5, 3, 10, C_WHIT);
    _canvas->fillRect(btnX + 1, localY + 5, 3, 10, C_WHIT);
  });
}

void GameEngine::drawMenu(int offsetY) {
  // Draw Background Maze (Brighter Blue)
  uint16_t wallColor = 0x3186;
  for (int y = 0; y < MAZE_HEIGHT; y++) {
    for (int x = 0; x < MAZE_WIDTH; x++) {
      int screenX = MAZE_OFFSET_X + x * TILE_SIZE;
      int screenY = MAZE_OFFSET_Y + y * TILE_SIZE - offsetY;
      if (screenY < -TILE_SIZE || screenY >= 32)
        continue;
      if (_maze[y][x] == 1)
        _canvas->drawRect(screenX, screenY, TILE_SIZE, TILE_SIZE, wallColor);
    }
  }

  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);
  if (offsetY < 100) {
    _canvas->setTextColor(C_YELL);
    _canvas->setTextSize(3);
    _canvas->drawString("PAC-MAN", SCREEN_W / 2, 60 - offsetY);
    _canvas->setTextSize(1);
  }
  if (offsetY > 160 && offsetY < 240) {
    uint16_t btnColor = C_ORNG;
    _canvas->fillRoundRect(120, 200 - offsetY, 240, 50, 15, btnColor);
    if (_selectedMenuItem == 0) {
      _canvas->drawRoundRect(118, 198 - offsetY, 244, 54, 15, C_WHIT);
      _canvas->drawRoundRect(119, 199 - offsetY, 242, 52, 15, C_WHIT);
    }
    _canvas->setTextColor(TFT_BLACK);
    _canvas->drawString("JUGAR", SCREEN_W / 2, 225 - offsetY);
  }
  if (offsetY > 220 && offsetY < 300) {
    uint16_t btnColor = C_BLUE;
    _canvas->fillRoundRect(120, 260 - offsetY, 240, 50, 15, btnColor);
    if (_selectedMenuItem == 1) {
      _canvas->drawRoundRect(118, 258 - offsetY, 244, 54, 15, C_WHIT);
      _canvas->drawRoundRect(119, 259 - offsetY, 242, 52, 15, C_WHIT);
    }
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("TIENDA", SCREEN_W / 2, 285 - offsetY);
  }
  if (offsetY < 70) {
    uint16_t btnColor = C_RED;
    _canvas->fillRoundRect(SCREEN_W - 80, 20 - offsetY, 60, 40, 10, btnColor);
    if (_selectedMenuItem == 2)
      _canvas->drawRoundRect(SCREEN_W - 82, 18 - offsetY, 64, 44, 10, C_WHIT);
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("SALIR", SCREEN_W - 50, 40 - offsetY);
  }
}

void GameEngine::drawPauseMenu(int offsetY) {
  int panelWidth = 280;
  int panelX = (SCREEN_W - panelWidth) / 2;
  int panelY = 85;
  if (offsetY + 32 >= panelY && offsetY <= panelY + 150) {
    int localY = panelY - offsetY;
    if (localY >= 0 && localY < 32) {
      _canvas->fillRoundRect(panelX, localY, panelWidth, 150, 10, TFT_DARKGREY);
      _canvas->drawRoundRect(panelX, localY, panelWidth, 150, 10, C_WHIT);
    } else if (localY < 0 && localY + 150 > 0) {
      _canvas->fillRect(panelX, 0, panelWidth, min(32, localY + 150),
                        TFT_DARKGREY);
      _canvas->drawFastVLine(panelX, 0, min(32, localY + 150), C_WHIT);
      _canvas->drawFastVLine(panelX + panelWidth - 1, 0, min(32, localY + 150),
                             C_WHIT);
    }
    _canvas->setTextFont(2);
    _canvas->setTextDatum(MC_DATUM);
    int titleY = 115 - offsetY;
    if (titleY >= 0 && titleY < 32) {
      _canvas->setTextColor(C_YELL);
      _canvas->drawString("PAUSADO", SCREEN_W / 2, titleY);
    }
    int btnResumeY = 150 - offsetY;
    if (btnResumeY >= -35 && btnResumeY < 32) {
      uint16_t btnColor = (_selectedPauseOption == 0) ? 0x0480 : C_GREN;
      _canvas->fillRoundRect(panelX + 40, btnResumeY, panelWidth - 80, 35, 8,
                             btnColor);
      if (_selectedPauseOption == 0)
        _canvas->drawRoundRect(panelX + 38, btnResumeY - 2, panelWidth - 76, 39,
                               8, C_WHIT);
      _canvas->setTextColor(TFT_BLACK);
      _canvas->drawString("CONTINUAR", SCREEN_W / 2, btnResumeY + 17);
    }
    int btnMenuY = 200 - offsetY;
    if (btnMenuY >= -35 && btnMenuY < 32) {
      uint16_t btnColor = (_selectedPauseOption == 1) ? 0x8000 : C_RED;
      _canvas->fillRoundRect(panelX + 40, btnMenuY, panelWidth - 80, 35, 8,
                             btnColor);
      if (_selectedPauseOption == 1)
        _canvas->drawRoundRect(panelX + 38, btnMenuY - 2, panelWidth - 76, 39,
                               8, C_WHIT);
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("MENU", SCREEN_W / 2, btnMenuY + 17);
    }
  }
}

void GameEngine::drawGameOver(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);
  if (offsetY < 150) {
    _canvas->setTextColor(C_RED);
    _canvas->drawString("GAME OVER", SCREEN_W / 2, 100 - offsetY);
  }
  if (offsetY > 100 && offsetY < 250) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("SCORE: " + String(_score), SCREEN_W / 2,
                        140 - offsetY);
    if (_score == _highScore && _score > 0) {
      _canvas->setTextColor(C_YELL);
      _canvas->drawString("NEW HIGH SCORE!", SCREEN_W / 2, 170 - offsetY);
    }
    if ((millis() / 500) % 2 == 0) {
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("TAP TO CONTINUE", SCREEN_W / 2, 220 - offsetY);
    }
  }
}

void GameEngine::drawWinScreen(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);
  if (offsetY < 150) {
    _canvas->setTextColor(C_GREN);
    _canvas->drawString("YOU WIN!", SCREEN_W / 2, 100 - offsetY);
  }
}

void GameEngine::drawShop(int offsetY) {
  // Fill background with solid color to ensure visibility
  _canvas->fillSprite(TFT_BLACK);

  if (offsetY < 50) {
    _canvas->fillRect(0, 0 - offsetY, SCREEN_W, 50, 0x1082);
    _canvas->setTextColor(C_YELL);
    _canvas->drawString("TIENDA", SCREEN_W / 2, 15 - offsetY);
    _canvas->setTextColor(C_ORNG);
    _canvas->fillCircle(SCREEN_W / 2 - 40, 40 - offsetY, 6, C_YELL);
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString(String(_totalCoins), SCREEN_W / 2 + 10, 40 - offsetY);
  }
  if (offsetY < 100) {
    int btnY = 65 - offsetY;
    if (btnY >= -40 && btnY < 32) {
      uint16_t btnColor = (_selectedShopItem == 12) ? 0x8000 : C_RED;
      _canvas->fillRoundRect(SCREEN_W / 2 - 60, btnY, 120, 35, 8, btnColor);
      if (_selectedShopItem == 12)
        _canvas->drawRoundRect(SCREEN_W / 2 - 62, btnY - 2, 124, 39, 8, C_WHIT);
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("< VOLVER", SCREEN_W / 2, btnY + 17);
    }
  }
  int boxWidth = 100;
  int boxHeight = 90;
  int spacing = 15;
  int startX = 20;
  int startY = 120;
  for (int i = 0; i < 12; i++) {
    int col = i % 4;
    int row = i / 4;
    int boxX = startX + col * (boxWidth + spacing);
    int boxY =
        startY + row * (boxHeight + spacing) - offsetY - _shopScrollOffset;
    if (boxY + boxHeight >= 0 && boxY < 32) {
      bool isSkin = (i < 8);
      int itemIndex = isSkin ? i : (i - 8);
      bool owned = isSkin ? _ownedSkins[itemIndex] : _ownedThemes[itemIndex];
      bool selected =
          isSkin ? (_selectedSkin == itemIndex) : (_selectedTheme == itemIndex);
      bool highlighted = (_selectedShopItem == i);
      if (highlighted)
        _canvas->fillRoundRect(boxX - 2, boxY - 2, boxWidth + 4, boxHeight + 4,
                               10, C_YELL);
      uint16_t boxColor =
          (owned && selected) ? 0x0480 : (owned ? 0x4208 : 0x2104);
      _canvas->fillRoundRect(boxX, boxY, boxWidth, boxHeight, 8, boxColor);
      uint16_t borderColor = highlighted ? C_YELL : (owned ? C_WHIT : 0x8410);
      _canvas->drawRoundRect(boxX, boxY, boxWidth, boxHeight, 8, borderColor);
      // Preview...
      int previewY = boxY + 20;
      if (previewY >= -30 && previewY < 32) {
        if (isSkin) {
          uint16_t skinColor = C_YELL;
          if (itemIndex == 1)
            skinColor = C_PINK;
          else if (itemIndex == 2)
            skinColor = C_CYAN;
          else if (itemIndex == 3)
            skinColor = C_GREN;
          else if (itemIndex == 4)
            skinColor = 0xF800;
          else if (itemIndex == 5)
            skinColor = 0x7E0;
          else if (itemIndex == 6)
            skinColor = 0x001F;
          else if (itemIndex == 7)
            skinColor = 0xFFFF;
          _canvas->fillCircle(boxX + boxWidth / 2, previewY, 14, skinColor);
        } else {
          uint16_t wallColor = C_BLUE;
          if (itemIndex == 1)
            wallColor = 0xD820;
          else if (itemIndex == 2)
            wallColor = 0x3540;
          else if (itemIndex == 3)
            wallColor = 0xFD20;
          _canvas->drawRect(boxX + boxWidth / 2 - 15, previewY - 15, 30, 30,
                            wallColor);
        }
      }
      // Price / Status
      int textY = boxY + 60;
      if (textY >= -10 && textY < 32) {
        _canvas->setTextSize(1);
        _canvas->setTextDatum(MC_DATUM);
        if (owned) {
          _canvas->setTextColor(C_GREN);
          _canvas->drawString("OWNED", boxX + boxWidth / 2, textY);
        } else {
          _canvas->setTextColor(C_YELL);
          _canvas->drawString(isSkin ? "50" : "100", boxX + boxWidth / 2,
                              textY);
        }
      }
    }
  }
}

bool GameEngine::isValidMove(int x, int y, Direction dir) {
  int nx = x, ny = y;
  switch (dir) {
  case DIR_UP:
    ny--;
    break;
  case DIR_DOWN:
    ny++;
    break;
  case DIR_LEFT:
    nx--;
    break;
  case DIR_RIGHT:
    nx++;
    break;
  default:
    return false;
  }
  if (nx < 0 || nx >= MAZE_WIDTH || ny < 0 || ny >= MAZE_HEIGHT)
    return false;
  if (_maze[ny][nx] == 1)
    return false; // Wall
  if (_maze[ny][nx] == 4)
    return false; // Cage (Pacman cannot enter)
  return true;
}

Position GameEngine::getNextPosition(Position pos, Direction dir) {
  Position next = pos;
  switch (dir) {
  case DIR_UP:
    next.y--;
    break;
  case DIR_DOWN:
    next.y++;
    break;
  case DIR_LEFT:
    next.x--;
    break;
  case DIR_RIGHT:
    next.x++;
    break;
  }
  if (next.x < 0)
    next.x = MAZE_WIDTH - 1;
  else if (next.x >= MAZE_WIDTH)
    next.x = 0;
  return next;
}

Direction GameEngine::getOppositeDirection(Direction dir) {
  switch (dir) {
  case DIR_UP:
    return DIR_DOWN;
  case DIR_DOWN:
    return DIR_UP;
  case DIR_LEFT:
    return DIR_RIGHT;
  case DIR_RIGHT:
    return DIR_LEFT;
  default:
    return DIR_NONE;
  }
}

int GameEngine::manhattanDistance(Position a, Position b) {
  return abs(a.x - b.x) + abs(a.y - b.y);
}

void GameEngine::returnToMenu() { ::returnToMenu(); }
