#include "GameEngine.h"
#include "Assets.h"
#include "System.h"

Preferences preferences;

#define SCREEN_W 480
#define SCREEN_H 320

GameEngine::GameEngine(TFT_eSPI *tft, Input *input) : _tft(tft), _input(input) {
  _canvas = new TFT_eSprite(tft);
  _state = STATE_MENU;
  _score = 0;
  _highScore = 0;
  _coins = 0;
  _equippedSkin = 0;
  _useSprite = false;
  _waveNumber = 0;
  _enemiesKilled = 0;
  _bossActive = false;
  _powerupTimer = 0;
  _weaponPowerupActive = false;
  _bossShootTimer = 0;
  _shopScroll = 0;
  _lastTouchX = 0;
  _wasTouching = false;
  _initialTouchX = 0;
  _lastClickTime = 0;
  _clickDebounce = false;
  _selectedMenuItem = 0;
  _selectedShopItem = 0;
  _selectedPauseOption = 0;
  _lastJoystickTime = 0;
  _lastJoystickDir = -1;
  _showWaveText = false;
  _waveTextTimer = 0;
}

void GameEngine::init() {
  preferences.begin("spaceshooter", false);
  loadGameData();

  _canvas->setColorDepth(16);
  void *ptr = _canvas->createSprite(SCREEN_W, 32);
  if (!ptr) {
    Serial.println("Failed to create strip sprite!");
    _useSprite = false;
  } else {
    Serial.println("Strip sprite created successfully!");
    _useSprite = true;
  }

  for (int i = 0; i < 50; i++) {
    _stars.push_back({(float)random(SCREEN_W), (float)random(SCREEN_H),
                      (float)random(1, 4),
                      (uint16_t)(random(0, 2) ? C_WHIT : C_GREY)});
  }

  _canvas->setTextFont(2);
}

void GameEngine::loadGameData() {
  _coins = preferences.getInt("coins", 0);
  _highScore = preferences.getInt("highScore", 0);
  _equippedSkin = preferences.getInt("equippedSkin", 0);

  // Marcar skins compradas
  for (int i = 0; i < NUM_SKINS; i++) {
    shopSkins[i].purchased =
        preferences.getBool(("skin_" + String(i)).c_str(), i == 0);
  }
}

void GameEngine::saveGameData() {
  preferences.putInt("coins", _coins);
  preferences.putInt("highScore", _highScore);
  preferences.putInt("equippedSkin", _equippedSkin);

  for (int i = 0; i < NUM_SKINS; i++) {
    preferences.putBool(("skin_" + String(i)).c_str(), shopSkins[i].purchased);
  }
}

void GameEngine::startGame() {
  _state = STATE_PLAYING;
  _score = 0;
  _waveNumber = 0;
  _enemiesKilled = 0;
  _bossActive = false;
  _powerupTimer = 0;
  _weaponPowerupActive = false;
  _gameStartTime = millis();
  _player = {
      SCREEN_W / 2.0f, SCREEN_H - 50.0f, 0, 0, 32, 32, 0, true, 100, C_CYAN, 0};
  _enemies.clear();
  _bullets.clear();
  _particles.clear();
  _powerups.clear();
}

int GameEngine::getDifficultyLevel() {
  if (_score < 500)
    return 1;
  if (_score < 1500)
    return 2;
  if (_score < 3000)
    return 3;
  return 4;
}

void GameEngine::update(float dt) {
  Point touch = _input->getTouch(SCREEN_W, SCREEN_H);
  JoystickInput joy = _input->getJoystick();
  ButtonInput btn = _input->getButtons();
  static unsigned long winTime = 0;
  static unsigned long lastEnemySpawnTime = 0;

  // SISTEMA ANTIRREBOTES - Evitar clicks accidentales
  unsigned long currentTime = millis();
  if (_clickDebounce && currentTime - _lastClickTime > 1000) { // 1 segundo
    _clickDebounce = false;
  }

  // Si hay rebote activo, ignorar todos los toques
  if (_clickDebounce) {
    touch.touched = false;
  }

  if (_state == STATE_WIN) {
    // Button A to return to menu
    if (btn.aJustPressed && millis() - winTime >= 1000 && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      _state = STATE_MENU;
    }

    if (touch.touched && millis() - winTime >= 1000 && !_clickDebounce) {
      if (touch.y > 220 && touch.y < 270 && touch.x > 120 && touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_MENU;
      }
    }
    return;
  }

  if (_state == STATE_SHOP) {
    // Joystick navigation for shop - only trigger on direction change
    if (joy.active && currentTime - _lastJoystickTime > 300) {
      int currentDir = (int)joy.direction;
      // Only change if direction is different from last time
      if (currentDir != _lastJoystickDir) {
        if (joy.direction == INPUT_DIR_LEFT) {
          _selectedShopItem = (_selectedShopItem - 1 + NUM_SKINS) % NUM_SKINS;
          _shopScroll = _selectedShopItem * 160;
          _lastJoystickTime = currentTime;
          _lastJoystickDir = currentDir;
        } else if (joy.direction == INPUT_DIR_RIGHT) {
          _selectedShopItem = (_selectedShopItem + 1) % NUM_SKINS;
          _shopScroll = _selectedShopItem * 160;
          _lastJoystickTime = currentTime;
          _lastJoystickDir = currentDir;
        }
      }
    } else if (!joy.active) {
      // Reset direction when joystick is released
      _lastJoystickDir = -1;
    }

    // Button A to purchase/equip skin
    if (btn.aJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      if (shopSkins[_selectedShopItem].purchased) {
        equipSkin(_selectedShopItem);
      } else if (_coins >= shopSkins[_selectedShopItem].price) {
        purchaseSkin(_selectedShopItem);
      }
    }

    // Button B to go back to menu
    if (btn.bJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      _state = STATE_MENU;
      _shopScroll = 0;
    }

    if (touch.touched) {
      if (!_wasTouching) {
        // Primer toque - guardar posición inicial
        _lastTouchX = touch.x;
        _initialTouchX = touch.x;
        _wasTouching = true;
      } else {
        // Scroll horizontal mientras se arrastra
        int deltaX = touch.x - _lastTouchX;
        _shopScroll -= deltaX * 2;
        _lastTouchX = touch.x;

        // Limitar scroll
        int maxScroll = (NUM_SKINS - 1) * 160;
        _shopScroll = constrain(_shopScroll, 0, maxScroll);
      }

      // Botón BACK
      if (!_clickDebounce && touch.x < 100 && touch.y < 60) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_MENU;
        _shopScroll = 0;
      }

      // Verificar toques en botones de skins (con antirrebotes)
      if (!_clickDebounce && abs(touch.x - _initialTouchX) < 10) {
        for (int i = 0; i < NUM_SKINS; i++) {
          int cardCenterX = 240 + i * 160 - _shopScroll;

          if (cardCenterX >= 100 && cardCenterX <= SCREEN_W - 100) {
            int buttonY = 150 + 58;
            if (touch.y > buttonY - 12 && touch.y < buttonY + 12 &&
                touch.x > cardCenterX - 50 && touch.x < cardCenterX + 50) {

              _lastClickTime = currentTime;
              _clickDebounce = true;

              if (shopSkins[i].purchased) {
                equipSkin(i);
              } else if (_coins >= shopSkins[i].price) {
                purchaseSkin(i);
              }
              break;
            }
          }
        }
      }
    } else {
      _wasTouching = false;
    }
  } else if (_state == STATE_MENU) {
    // Joystick navigation for menu
    if (joy.active && currentTime - _lastJoystickTime > 200) {
      if (joy.direction == INPUT_DIR_UP) {
        _selectedMenuItem = (_selectedMenuItem - 1 + 3) % 3;
        _lastJoystickTime = currentTime;
      } else if (joy.direction == INPUT_DIR_DOWN) {
        _selectedMenuItem = (_selectedMenuItem + 1) % 3;
        _lastJoystickTime = currentTime;
      }
    }

    // Button A to select menu item
    if (btn.aJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      if (_selectedMenuItem == 0) {
        startGame();
      } else if (_selectedMenuItem == 1) {
        _state = STATE_SHOP;
      } else if (_selectedMenuItem == 2) {
        returnToMainMenu();
      }
    }

    if (touch.touched && !_clickDebounce) {
      // Botón START
      if (touch.y > 200 && touch.y < 250 && touch.x > 120 && touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        startGame();
      }
      // Botón SHOP
      else if (touch.y > 260 && touch.y < 310 && touch.x > 120 &&
               touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_SHOP;
      }
      // Botón EXIT
      else if (touch.x > SCREEN_W - 80 && touch.x < SCREEN_W - 20 &&
               touch.y > 20 && touch.y < 60) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        returnToMenu(); // Declared in System.h
      }
    }
  } else if (_state == STATE_PLAYING) {
    // Botón PAUSE (con antirrebotes)
    if ((touch.touched && !_clickDebounce && touch.x > SCREEN_W - 70 &&
         touch.x < SCREEN_W - 10 && touch.y > 10 && touch.y < 50) ||
        (btn.bJustPressed && !_clickDebounce)) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      _state = STATE_PAUSED;
      return;
    }

    // Joystick control for player movement
    if (joy.active) {
      // Move player based on joystick input
      float speed = 15.0f; // Increased speed for better responsiveness
      _player.x += (joy.x / 2048.0f) * speed;
      _player.y += (joy.y / 2048.0f) * speed;
    }

    // Button A to shoot
    static int shootTimer = 0;
    if (btn.aPressed) {
      shootTimer++;
      int fireRate = _weaponPowerupActive ? 1 : 2;
      if (shootTimer > fireRate) {
        _bullets.push_back(
            {_player.x, _player.y - 16, 0, -15, 4, 8, 2, true, 1, C_YELL, 0});
        shootTimer = 0;
      }
    } else {
      shootTimer = 0;
    }

    // Control del jugador - VERSIÓN MÁS RÁPIDA (touch control)
    if (touch.touched) {
      float dx = touch.x - _player.x;
      float dy = (touch.y - 40) - _player.y;

      // MOVIMIENTO MÁS RÁPIDO
      _player.x += dx * 0.8f;
      _player.y += dy * 0.8f;

      static int touchShootTimer = 0;
      touchShootTimer++;

      // DISPARO MÁS RÁPIDO (1/2 en lugar de 2/4)
      int fireRate = _weaponPowerupActive ? 1 : 2;
      if (touchShootTimer > fireRate) {
        // BALAS MÁS RÁPIDAS (vy = -15 en lugar de -8)
        _bullets.push_back(
            {_player.x, _player.y - 16, 0, -15, 4, 8, 2, true, 1, C_YELL, 0});
        touchShootTimer = 0;
      }
    }

    _player.x = constrain(_player.x, 16, SCREEN_W - 16);
    _player.y = constrain(_player.y, 16, SCREEN_H - 16);

    if (_weaponPowerupActive && millis() > _weaponPowerupEnd) {
      _weaponPowerupActive = false;
    }

    // AGREGAR ESTA LÓGICA PARA GENERAR ENEMIGOS
    unsigned long currentTime = millis();

    // Generar nueva oleada si no hay enemigos y ha pasado el tiempo suficiente
    if (_enemies.empty() && !_bossActive &&
        currentTime - lastEnemySpawnTime > 3000) { // 3 segundos entre oleadas
      spawnEnemyWave();
      lastEnemySpawnTime = currentTime;
    }

    updateEnemies(dt);
    updateBullets(dt);
    updateParticles(dt);
    updatePowerups(dt);
    if (_bossActive)
      updateBoss(dt);
    checkCollisions();

    // Spawn del boss (solo si no hay uno activo)
    if (_score >= 2000 && !_bossActive && _enemies.size() == 0) {
      _waveText = "FINAL BOSS";
      _showWaveText = true;
      _waveTextTimer = 180;
      spawnBoss();
    }

    // CONDICIÓN DE VICTORIA
    if (_bossActive && !_boss.active && _enemies.size() == 0) {
      _state = STATE_WIN;
      winTime = millis(); // Guardar tiempo de victoria
      _coins += _score / 10 + 500;
      if (_score > _highScore)
        _highScore = _score;
      saveGameData();
    }

    _powerupTimer++;
    if (_powerupTimer > random(600, 900)) {
      spawnPowerup();
      _powerupTimer = 0;
    }

    // Game Over - AHORA DA MONEDAS AL PERDER TAMBIÉN
    if (_player.health <= 0) {
      if (_score > _highScore) {
        _highScore = _score;
      }
      _coins += _score / 20; // Monedas al perder = 5% del score
      _state = STATE_GAMEOVER;
      saveGameData();
    }
  } else if (_state == STATE_PAUSED) {
    // Joystick navigation for pause menu
    if (joy.active && currentTime - _lastJoystickTime > 200) {
      if (joy.direction == INPUT_DIR_UP) {
        _selectedPauseOption = (_selectedPauseOption - 1 + 2) % 2;
        _lastJoystickTime = currentTime;
      } else if (joy.direction == INPUT_DIR_DOWN) {
        _selectedPauseOption = (_selectedPauseOption + 1) % 2;
        _lastJoystickTime = currentTime;
      }
    }

    // Button A to select pause menu option
    if (btn.aJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      if (_selectedPauseOption == 0) {
        _state = STATE_PLAYING;
      } else {
        _state = STATE_MENU;
      }
    }

    // Button B to resume
    if (btn.bJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      _state = STATE_PLAYING;
    }

    if (touch.touched && !_clickDebounce) {
      // Botón RESUME
      if (touch.y > 120 && touch.y < 170 && touch.x > 120 && touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_PLAYING;
      }
      // Botón EXIT TO MENU
      else if (touch.y > 190 && touch.y < 240 && touch.x > 120 &&
               touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_MENU;
      }
    }
  } else if (_state == STATE_GAMEOVER) {
    // Button A to return to menu
    if (btn.aJustPressed && !_clickDebounce) {
      _lastClickTime = currentTime;
      _clickDebounce = true;
      _state = STATE_MENU;
    }

    if (touch.touched && !_clickDebounce) {
      // Botón más grande en GAME OVER
      if (touch.y > 220 && touch.y < 270 && touch.x > 120 && touch.x < 360) {
        _lastClickTime = currentTime;
        _clickDebounce = true;
        _state = STATE_MENU;
      }
    }
  }
  // Actualizar estrellas de fondo
  for (auto &s : _stars) {
    s.y += s.speed;
    if (s.y >= SCREEN_H) {
      s.y = 0;
      s.x = random(SCREEN_W);
    }
  }
}

void GameEngine::purchaseSkin(int skinId) {
  if (skinId >= 0 && skinId < NUM_SKINS && !shopSkins[skinId].purchased) {
    if (_coins >= shopSkins[skinId].price) {
      _coins -= shopSkins[skinId].price;
      shopSkins[skinId].purchased = true;
      equipSkin(skinId);
      saveGameData();
    }
  }
}

void GameEngine::equipSkin(int skinId) {
  if (skinId >= 0 && skinId < NUM_SKINS && shopSkins[skinId].purchased) {
    _equippedSkin = skinId;
    saveGameData();
  }
}

void GameEngine::returnToMainMenu() {
  _state = STATE_MENU;
  saveGameData();
}

void GameEngine::spawnEnemyWave() {
  _waveNumber++;
  _waveText = "WAVE " + String(_waveNumber);
  _showWaveText = true;
  _waveTextTimer = 120; // 2 seconds

  int pattern = random(0, 3);
  spawnFormation(pattern);
}

void GameEngine::spawnFormation(int type) {
  int difficulty = getDifficultyLevel();
  int enemyCount = 3 + difficulty;
  int enemyHealth = difficulty;

  for (int i = 0; i < enemyCount; i++) {
    Entity e;
    e.type = 1;
    e.active = true;
    e.health = enemyHealth;
    e.color = C_RED;
    e.width = 24;
    e.height = 24;
    e.vx = 0;
    e.vy = 0; // Controlled by state
    e.animFrame = 0;
    e.state = 0; // ENTRANCE

    // Start off-screen
    e.x = random(50, SCREEN_W - 50);
    e.y = -50;

    // Set target position based on formation
    switch (type) {
    case 0: // Line
      e.targetX = 50 + i * (SCREEN_W - 100) / (enemyCount - 1);
      e.targetY = 50;
      break;
    case 1: // V-Shape
      e.targetX = SCREEN_W / 2 + (i - enemyCount / 2) * 60;
      e.targetY = 50 + abs(i - enemyCount / 2) * 40;
      break;
    case 2: // Grid/Randomish
      e.targetX = 60 + (i % 3) * 120;
      e.targetY = 40 + (i / 3) * 60;
      break;
    }

    _enemies.push_back(e);
  }
}

void GameEngine::spawnPowerup() {
  Entity p;
  p.type = 4;
  p.x = random(50, SCREEN_W - 50);
  p.y = -20;
  p.vx = 0;
  p.vy = 2;
  p.width = 16;
  p.height = 16;
  p.active = true;
  p.health = random(0, 2);
  p.color = p.health == 0 ? C_BLUE : C_ORNG;
  p.animFrame = 0;
  _powerups.push_back(p);
}

void GameEngine::spawnBoss() {
  _bossActive = true;
  _bossShootTimer = 0;
  _boss = {SCREEN_W / 2.0f, 60.0f, 1.5f, 0, 48, 48, 5, true, 100, C_RED, 0};
}

void GameEngine::updateEnemies(float dt) {
  for (auto &e : _enemies) {
    if (e.state == 0) { // ENTRANCE
      float dx = e.targetX - e.x;
      float dy = e.targetY - e.y;
      e.x += dx * 0.05f;
      e.y += dy * 0.05f;

      if (abs(dx) < 2 && abs(dy) < 2) {
        e.state = 1; // ATTACK
      }
    } else {       // ATTACK
      e.y += 1.5f; // Constant speed down
      e.x += sin(millis() / 200.0 + e.y * 0.05) * 2.0;
    }

    if (e.y > SCREEN_H + 20)
      e.active = false;
  }

  // Limpiar enemigos inactivos
  for (int i = _enemies.size() - 1; i >= 0; i--) {
    if (!_enemies[i].active) {
      _enemies.erase(_enemies.begin() + i);
      _enemiesKilled++;
    }
  }
}

void GameEngine::updateBoss(float dt) {
  if (!_boss.active)
    return;

  _boss.x += _boss.vx;
  if (_boss.x < 50 || _boss.x > SCREEN_W - 50) {
    _boss.vx = -_boss.vx;
  }

  _bossShootTimer++;
  if (_bossShootTimer > 120) {
    for (int i = -1; i <= 1; i++) {
      _bullets.push_back(
          {_player.x, _player.y - 16, 0, -20, 4, 8, 2, true, 1, C_YELL, 0});
    }
    _bossShootTimer = 0;
  }
}

void GameEngine::updateBullets(float dt) {
  for (auto &b : _bullets) {
    b.y += b.vy;
    b.x += b.vx;
    if (b.y < -10 || b.y > SCREEN_H + 10 || b.x < 0 || b.x > SCREEN_W)
      b.active = false;
  }
  for (int i = _bullets.size() - 1; i >= 0; i--) {
    if (!_bullets[i].active)
      _bullets.erase(_bullets.begin() + i);
  }
}

void GameEngine::updateParticles(float dt) {
  for (auto &p : _particles) {
    p.x += p.vx;
    p.y += p.vy;
    p.health--;
    if (p.type == 3) {
      p.animFrame = (28 - p.health) / 7;
    }
    if (p.health <= 0)
      p.active = false;
  }
  for (int i = _particles.size() - 1; i >= 0; i--) {
    if (!_particles[i].active)
      _particles.erase(_particles.begin() + i);
  }
}

void GameEngine::updatePowerups(float dt) {
  for (auto &p : _powerups) {
    p.y += p.vy;
    if (p.y > SCREEN_H + 20)
      p.active = false;
  }
  for (int i = _powerups.size() - 1; i >= 0; i--) {
    if (!_powerups[i].active)
      _powerups.erase(_powerups.begin() + i);
  }
}

void GameEngine::checkCollisions() {
  for (auto &b : _bullets) {
    if (!b.active || b.vy > 0)
      continue;

    for (auto &e : _enemies) {
      if (!e.active)
        continue;
      if (abs(b.x - e.x) < (e.width / 2 + b.width / 2) &&
          abs(b.y - e.y) < (e.height / 2 + b.height / 2)) {
        b.active = false;
        e.health--;
        if (e.health <= 0) {
          e.active = false;
          createExplosion(e.x, e.y, C_ORNG);
          _score += 100;
        }
      }
    }

    if (_bossActive && _boss.active) {
      if (abs(b.x - _boss.x) < (_boss.width / 2 + b.width / 2) &&
          abs(b.y - _boss.y) < (_boss.height / 2 + b.height / 2)) {
        b.active = false;
        _boss.health--;
        if (_boss.health <= 0) {
          _boss.active = false;
          _bossActive = false; // Asegurar que el boss está inactivo
          createExplosion(_boss.x, _boss.y, C_ORNG);
          _score += 1000;
          // CONDICIÓN DE VICTORIA INMEDIATA
          _state = STATE_WIN;
          _coins += _score / 10 + 500; // Bonus por matar al boss
          if (_score > _highScore)
            _highScore = _score;
          saveGameData();
        }
      }
    }
  }

  for (auto &b : _bullets) {
    if (!b.active || b.vy < 0)
      continue;
    if (abs(b.x - _player.x) < (_player.width / 2 + b.width / 2) &&
        abs(b.y - _player.y) < (_player.height / 2 + b.height / 2)) {
      b.active = false;
      _player.health -= 10;
      createExplosion(b.x, b.y, C_RED);
    }
  }

  for (auto &e : _enemies) {
    if (!e.active)
      continue;
    if (abs(e.x - _player.x) < (e.width / 2 + _player.width / 2) &&
        abs(e.y - _player.y) < (e.height / 2 + _player.height / 2)) {
      e.active = false;
      _player.health -= 20;
      createExplosion(e.x, e.y, C_RED);
      createExplosion(_player.x, _player.y, C_RED);
    }
  }

  for (auto &p : _powerups) {
    if (!p.active)
      continue;
    if (abs(p.x - _player.x) < (p.width / 2 + _player.width / 2) &&
        abs(p.y - _player.y) < (p.height / 2 + _player.height / 2)) {
      p.active = false;
      if (p.health == 0) {
        _player.health = min(100, _player.health + 50);
      } else {
        _weaponPowerupActive = true;
        _weaponPowerupEnd = millis() + 10000;
      }
    }
  }
}

void GameEngine::spawnEnemy() {}

void GameEngine::createExplosion(float x, float y, uint16_t color) {
  Entity p;
  p.x = x;
  p.y = y;
  p.vx = 0;
  p.vy = 0;
  p.health = 28;
  p.color = color;
  p.active = true;
  p.type = 3;
  p.animFrame = 0;
  p.width = 16;
  p.height = 16;
  _particles.push_back(p);
}

void GameEngine::draw() {
  if (!_useSprite) {
    _tft->fillScreen(TFT_BLACK);
    return;
  }

  for (int y = 0; y < SCREEN_H; y += 32) {
    _canvas->fillSprite(TFT_BLACK);

    for (auto &s : _stars) {
      int localY = (int)s.y - y;
      if (localY >= 0 && localY < 32)
        _canvas->drawPixel((int)s.x, localY, s.color);
    }

    if (_state == STATE_MENU) {
      drawMenu(y);
    } else if (_state == STATE_SHOP) {
      drawShop(y);
    } else if (_state == STATE_PLAYING) {
      drawPlayer(y);
      for (auto &e : _enemies)
        drawEnemy(e, y);
      for (auto &b : _bullets)
        drawBullet(b, y);
      for (auto &p : _powerups)
        drawPowerup(p, y);
      if (_bossActive && _boss.active)
        drawBoss(y);
      for (auto &p : _particles)
        drawExplosion(p, y);
      drawHUD(y);
    } else if (_state == STATE_PAUSED) {
      drawPlayer(y);
      for (auto &e : _enemies)
        drawEnemy(e, y);
      for (auto &b : _bullets)
        drawBullet(b, y);
      for (auto &p : _powerups)
        drawPowerup(p, y);
      if (_bossActive && _boss.active)
        drawBoss(y);
      for (auto &p : _particles)
        drawExplosion(p, y);
      drawPauseMenu(y);
    } else if (_state == STATE_GAMEOVER) {
      drawGameOver(y);
    } else if (_state == STATE_WIN) {
      drawWinScreen(y);
    }

    _canvas->pushSprite(0, y);
  }
}

void GameEngine::drawPlayer(int offsetY) {
  int localY = (int)_player.y - offsetY;
  if (localY < -20 || localY > 52)
    return;

  int startX = (int)_player.x - PLAYER_W / 2;
  int startY = localY - PLAYER_H / 2;

  // Usar la skin equipada
  const uint16_t *skin = player_skins[_equippedSkin];

  for (int y = 0; y < PLAYER_H; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < PLAYER_W; x++) {
      uint16_t color = pgm_read_word(&skin[y * PLAYER_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawEnemy(Entity &e, int offsetY) {
  int localY = (int)e.y - offsetY;
  if (localY < -15 || localY > 47)
    return;

  int startX = (int)e.x - ENEMY_W / 2;
  int startY = localY - ENEMY_H / 2;

  for (int y = 0; y < ENEMY_H; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < ENEMY_W; x++) {
      uint16_t color = pgm_read_word(&enemy_ship[y * ENEMY_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawBoss(int offsetY) {
  int localY = (int)_boss.y - offsetY;
  if (localY < -30 || localY > 62)
    return;

  int startX = (int)_boss.x - BOSS_W / 2;
  int startY = localY - BOSS_H / 2;

  for (int y = 0; y < BOSS_H; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < BOSS_W; x++) {
      uint16_t color = pgm_read_word(&boss_ship[y * BOSS_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawBullet(Entity &b, int offsetY) {
  int localY = (int)b.y - offsetY;
  if (localY < -10 || localY > 42)
    return;

  int startX = (int)b.x - BULLET_W / 2;
  int startY = localY - BULLET_H / 2;

  for (int y = 0; y < BULLET_H; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < BULLET_W; x++) {
      uint16_t color = pgm_read_word(&bullet_sprite[y * BULLET_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawPowerup(Entity &p, int offsetY) {
  int localY = (int)p.y - offsetY;
  if (localY < -10 || localY > 42)
    return;

  int startX = (int)p.x - POWERUP_W / 2;
  int startY = localY - POWERUP_H / 2;

  const uint16_t *sprite = (p.health == 0) ? powerup_shield : powerup_weapon;

  for (int y = 0; y < POWERUP_H; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < POWERUP_W; x++) {
      uint16_t color = pgm_read_word(&sprite[y * POWERUP_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawExplosion(Entity &p, int offsetY) {
  int localY = (int)p.y - offsetY;
  if (localY < -10 || localY > 42)
    return;

  int startX = (int)p.x - 8;
  int startY = localY - 8;

  const uint16_t *frame;
  switch (p.animFrame) {
  case 0:
    frame = explosion_frame1;
    break;
  case 1:
    frame = explosion_frame2;
    break;
  case 2:
    frame = explosion_frame3;
    break;
  default:
    frame = explosion_frame4;
    break;
  }

  for (int y = 0; y < 16; y++) {
    int screenY = startY + y;
    if (screenY < 0 || screenY >= 32)
      continue;

    for (int x = 0; x < 16; x++) {
      uint16_t color = pgm_read_word(&frame[y * 16 + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, screenY, color);
      }
    }
  }
}

void GameEngine::drawHUD(int offsetY) {
  if (offsetY > 40)
    return;

  _canvas->setTextFont(2);
  _canvas->setTextColor(C_WHIT);
  _canvas->setTextSize(1);

  // Score y monedas
  _canvas->setCursor(10, 10 - offsetY);
  _canvas->printf("SCORE: %d", _score);

  _canvas->setCursor(SCREEN_W - 400, 10 - offsetY);
  _canvas->printf("COINS: %d", _coins);

  // Barra de vida
  _canvas->drawRect(10, 25 - offsetY, 102, 12, C_WHIT);
  _canvas->fillRect(11, 26 - offsetY, _player.health, 10, C_RED);

  if (_bossActive && _boss.active && offsetY == 0) {
    _canvas->setCursor(SCREEN_W / 2 - 20, 10);
    _canvas->print("BOSS");
    _canvas->drawRect(SCREEN_W / 2 - 52, 25, 104, 8, C_RED);
    _canvas->fillRect(SCREEN_W / 2 - 51, 26, _boss.health, 6, C_RED);
  }

  // Botón PAUSE (más grande)
  _canvas->fillRoundRect(SCREEN_W - 70, 5 - offsetY, 60, 30, 5, C_ORNG);
  _canvas->setTextColor(TFT_BLACK);
  _canvas->setTextSize(1);
  _canvas->setTextDatum(MC_DATUM);
  _canvas->drawString("II", SCREEN_W - 40, 20 - offsetY);
  _canvas->setTextDatum(TL_DATUM);

  if (_showWaveText) {
    _waveTextTimer--;
    if (_waveTextTimer <= 0) {
      _showWaveText = false;
    } else {
      _canvas->setTextDatum(MC_DATUM);
      _canvas->setTextSize(2);
      _canvas->setTextColor(C_YELL);
      _canvas->drawString(_waveText, SCREEN_W / 2, SCREEN_H / 2 - offsetY);
      _canvas->setTextSize(1);
      _canvas->setTextDatum(TL_DATUM);
    }
  }
}

void GameEngine::drawMenu(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);

  // Título
  if (offsetY < 100) {
    int pulse = (millis() / 100) % 20;
    uint16_t glowColor = (pulse > 10) ? C_CYAN : C_BLUE;
    _canvas->setTextColor(glowColor);
    _canvas->setTextSize(2);
    _canvas->drawString("SPACE SHOOTER", SCREEN_W / 2, 60 - offsetY);
    _canvas->setTextSize(1);
  }

  // Información del jugador (CON TEXTO COMPLETO)
  if (offsetY < 150 && offsetY > 30) {
    _canvas->setTextColor(C_YELL);
    _canvas->drawString("Monedas: " + String(_coins), SCREEN_W / 2,
                        100 - offsetY);

    _canvas->setTextColor(C_CYAN);
    String skinText = "Skin: " + String(shopSkins[_equippedSkin].name);
    _canvas->drawString(skinText, SCREEN_W / 2, 120 - offsetY);

    if (_highScore > 0) {
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("Record: " + String(_highScore), SCREEN_W / 2,
                          140 - offsetY);
    }
  }

  // Botón START
  if (offsetY > 160 && offsetY < 240) {
    _canvas->fillRoundRect(120, 200 - offsetY, 240, 50, 15, C_ORNG);
    if (_selectedMenuItem == 0) {
      _canvas->drawRoundRect(118, 198 - offsetY, 244, 54, 15, C_WHIT);
      _canvas->drawRoundRect(119, 199 - offsetY, 242, 52, 15, C_WHIT);
    }
    _canvas->setTextColor(TFT_BLACK);
    _canvas->setTextSize(1);
    _canvas->drawString("JUGAR", SCREEN_W / 2, 225 - offsetY);
  }

  // Botón SHOP
  if (offsetY > 240 && offsetY < 320) {
    _canvas->fillRoundRect(120, 260 - offsetY, 240, 50, 15, C_PURP);
    if (_selectedMenuItem == 1) {
      _canvas->drawRoundRect(118, 258 - offsetY, 244, 54, 15, C_WHIT);
      _canvas->drawRoundRect(119, 259 - offsetY, 242, 52, 15, C_WHIT);
    }
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("TIENDA", SCREEN_W / 2, 285 - offsetY);
  }

  // Botón EXIT (mejorado)
  if (offsetY < 70) {
    _canvas->fillRoundRect(SCREEN_W - 80, 20 - offsetY, 60, 40, 10, C_RED);
    _canvas->setTextColor(C_WHIT);
    _canvas->setTextDatum(MC_DATUM);
    _canvas->drawString("SALIR", SCREEN_W - 50, 40 - offsetY);
    if (_selectedMenuItem == 2) {
      _canvas->drawRoundRect(SCREEN_W - 82, 18 - offsetY, 64, 44, 10, C_WHIT);
      _canvas->drawRoundRect(SCREEN_W - 81, 19 - offsetY, 62, 42, 10, C_WHIT);
    }
    _canvas->setTextDatum(TL_DATUM);
  }
}

void GameEngine::drawShop(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);

  // Fondo de título
  if (offsetY < 60) {
    _canvas->fillRect(0, 0 - offsetY, SCREEN_W, 50, C_DKGR);
    _canvas->setTextColor(C_CYAN);
    _canvas->setTextSize(2);
    _canvas->drawString("TIENDA", SCREEN_W / 2, 25 - offsetY);
  }

  // Información de monedas (MEJORADO - posición fija)
  if (offsetY < 80 && offsetY > 30) {
    _canvas->setTextColor(C_YELL);
    _canvas->setTextSize(1);
    String coinsText = "Monedas: " + String(_coins);
    _canvas->drawString(coinsText, SCREEN_W / 2, 55 - offsetY);
  }

  // Botón BACK
  if (offsetY < 70) {
    _canvas->fillRoundRect(20, 20 - offsetY, 70, 35, 8, C_BLUE);
    _canvas->setTextColor(C_WHIT);
    _canvas->setTextSize(1);
    _canvas->drawString("VOLVER", 55, 38 - offsetY);
  }

  // Indicador de scroll (puntos)
  if (offsetY > 250) {
    int currentIndex = _shopScroll / 160;
    for (int i = 0; i < NUM_SKINS; i++) {
      int dotX = SCREEN_W / 2 - (NUM_SKINS * 6) / 2 + i * 12;
      if (i == currentIndex) {
        _canvas->fillCircle(dotX, 295 - offsetY, 4, C_WHIT);
      } else {
        _canvas->drawCircle(dotX, 295 - offsetY, 3, C_GREY);
      }
    }
  }

  // Cards horizontales (usar _shopScroll en lugar de shopScroll local)
  for (int i = 0; i < NUM_SKINS; i++) {
    int cardCenterX = 240 + i * 160 - _shopScroll; // Usar _shopScroll miembro
    int cardY = 150 - offsetY;

    // Solo dibujar cards visibles
    if (cardCenterX < -80 || cardCenterX > SCREEN_W + 80)
      continue;

    // Card background
    _canvas->fillRoundRect(cardCenterX - 70, cardY - 80, 140, 180, 12, C_DKGR);
    _canvas->drawRoundRect(cardCenterX - 70, cardY - 80, 140, 180, 12, C_GREY);

    if (i == _selectedShopItem) {
      _canvas->drawRoundRect(cardCenterX - 72, cardY - 82, 144, 184, 14,
                             C_WHIT);
      _canvas->drawRoundRect(cardCenterX - 71, cardY - 81, 142, 182, 13,
                             C_WHIT);
    }

    // Borde especial si está equipado
    if (_equippedSkin == i && shopSkins[i].purchased) {
      _canvas->drawRoundRect(cardCenterX - 72, cardY - 82, 144, 184, 14,
                             C_GREN);
    }

    // Nombre del skin
    _canvas->setTextColor(C_WHIT);
    _canvas->setTextSize(1);
    _canvas->drawString(shopSkins[i].name, cardCenterX, cardY - 60);

    // Preview de la nave
    _canvas->fillRect(cardCenterX - 50, cardY - 40, 100, 80, TFT_BLACK);
    _canvas->drawRect(cardCenterX - 50, cardY - 40, 100, 80, C_WHIT);

    // Dibujar preview de la nave
    drawSkinPreview(cardCenterX, cardY, i);

    // Precio o estado
    if (shopSkins[i].purchased) {
      _canvas->setTextColor(C_CYAN);
      _canvas->drawString("COMPRADO", cardCenterX, cardY + 48);
    } else {
      _canvas->setTextColor(C_YELL);
      _canvas->drawString(String(shopSkins[i].price) + " coins", cardCenterX,
                          cardY + 48);
    }

    // Botón de acción
    uint16_t btnColor, textColor;
    String btnText;

    if (shopSkins[i].purchased) {
      if (_equippedSkin == i) {
        btnColor = C_GREN;
        textColor = TFT_BLACK;
        btnText = "EQUIPADO";
      } else {
        btnColor = C_BLUE;
        textColor = C_WHIT;
        btnText = "EQUIPAR";
      }
    } else {
      if (_coins >= shopSkins[i].price) {
        btnColor = C_ORNG;
        textColor = TFT_BLACK;
        btnText = "COMPRAR";
      } else {
        btnColor = C_DKGR;
        textColor = C_GREY;
        btnText = "NO ALCANZA";
      }
    }

    _canvas->fillRoundRect(cardCenterX - 50, cardY + 58, 100, 24, 6, btnColor);
    _canvas->setTextColor(textColor);
    _canvas->setTextSize(1);
    _canvas->drawString(btnText, cardCenterX, cardY + 70);
  }
}

// Asegúrate de que drawSkinPreview esté correctamente implementado:
void GameEngine::drawSkinPreview(int centerX, int centerY, int skinId) {
  const uint16_t *skin = player_skins[skinId];
  int startX = centerX - PLAYER_W / 2;
  int startY = centerY - PLAYER_H / 2;

  for (int y = 0; y < PLAYER_H; y++) {
    for (int x = 0; x < PLAYER_W; x++) {
      uint16_t color = pgm_read_word(&skin[y * PLAYER_W + x]);
      if (color != C_TRSP) {
        _canvas->drawPixel(startX + x, startY + y, color);
      }
    }
  }
}

// Función helper para obtener color de preview
uint16_t GameEngine::getSkinColor(int skinId) {
  switch (skinId) {
  case 0:
    return C_CYAN; // Default
  case 1:
    return C_BLUE; // Azul
  case 2:
    return C_RED; // Rojo
  case 3:
    return C_GREN; // Verde
  default:
    return C_WHIT;
  }
}

void GameEngine::drawWinScreen(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);

  if (offsetY < 150) {
    _canvas->setTextSize(2);
    _canvas->setTextColor(C_GREN);
    _canvas->drawString("VICTORIA!", SCREEN_W / 2, 100 - offsetY);
    _canvas->setTextSize(1);
  }

  if (offsetY > 100 && offsetY < 250) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("Score: " + String(_score), SCREEN_W / 2,
                        140 - offsetY);
    _canvas->drawString("Monedas ganadas: +" + String(_score / 10),
                        SCREEN_W / 2, 160 - offsetY);
    _canvas->drawString("Monedas total: " + String(_coins), SCREEN_W / 2,
                        180 - offsetY);

    if ((millis() / 500) % 2 == 0) {
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("TOCA PARA CONTINUAR", SCREEN_W / 2, 220 - offsetY);
    }
  }
}

void GameEngine::drawPauseMenu(int offsetY) {
  if (offsetY < 320) {
    _canvas->setTextFont(2);
    _canvas->setTextDatum(MC_DATUM);

    if (offsetY < 100) {
      _canvas->setTextColor(C_CYAN);
      _canvas->setTextSize(2);
      _canvas->drawString("PAUSED", SCREEN_W / 2, 80 - offsetY);
      _canvas->setTextSize(1);
    }

    if (offsetY > 100 && offsetY < 200) {
      _canvas->fillRect(140, 140 - offsetY, 200, 40, C_ORNG);
      _canvas->setTextColor(TFT_BLACK);
      _canvas->drawString("RESUME", SCREEN_W / 2, 160 - offsetY);
    }

    if (offsetY > 160 && offsetY < 260) {
      _canvas->fillRect(140, 200 - offsetY, 200, 40, C_RED);
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("EXIT", SCREEN_W / 2, 220 - offsetY);
    }
  }
}

void GameEngine::drawGameOver(int offsetY) {
  _canvas->setTextFont(2);
  _canvas->setTextDatum(MC_DATUM);

  if (offsetY < 150) {
    _canvas->setTextSize(2);
    _canvas->setTextColor(C_DKGR);
    _canvas->drawString("GAME OVER", SCREEN_W / 2 + 2, 122 - offsetY);
    _canvas->setTextColor(C_RED);
    _canvas->drawString("GAME OVER", SCREEN_W / 2, 120 - offsetY);
    _canvas->setTextSize(1);
  }

  if (offsetY > 120 && offsetY < 280) {
    _canvas->setTextColor(C_WHIT);
    _canvas->drawString("SCORE: " + String(_score), SCREEN_W / 2,
                        160 - offsetY);

    if (_score == _highScore && _score > 0) {
      _canvas->setTextColor(C_YELL);
      _canvas->drawString("NEW HIGH SCORE!", SCREEN_W / 2, 190 - offsetY);
    }

    if ((millis() / 500) % 2 == 0) {
      _canvas->setTextColor(C_WHIT);
      _canvas->drawString("TAP TO RESTART", SCREEN_W / 2, 240 - offsetY);
    }
  }
}
