#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "Input.h"
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <vector>

enum GameState {
  STATE_MENU,
  STATE_PLAYING,
  STATE_PAUSED,
  STATE_GAMEOVER,
  STATE_SHOP,
  STATE_WIN
};

struct Entity {
  float x, y;
  float vx, vy;
  int width, height;
  int type; // 0: Player, 1: Enemy, 2: Bullet, 3: Particle, 4: Powerup, 5: Boss
  bool active;
  int health;
  uint16_t color;
  int animFrame; // For explosions and animations

  // New fields for formations
  float targetX, targetY;
  int state; // 0: Entrance, 1: Attack
};

class GameEngine {
public:
  GameEngine(TFT_eSPI *tft, Input *input);
  void init();
  void update(float dt);
  void draw();

  void startGame();
  void stopGame();

private:
  TFT_eSPI *_tft;
  Input *_input;
  TFT_eSprite *_canvas;
  bool _useSprite;

  GameState _state;
  int _score;
  int _highScore;
  int _coins;        // Nuevo: sistema de monedas
  int _equippedSkin; // Skin equipada actualmente

  Entity _player;
  std::vector<Entity> _enemies;
  std::vector<Entity> _bullets;
  std::vector<Entity> _particles;
  std::vector<Entity> _powerups;

  // Boss
  bool _bossActive;
  Entity _boss;
  int _bossShootTimer;

  // Game progression
  int _waveNumber;
  int _enemiesKilled;
  unsigned long _gameStartTime;
  int _powerupTimer;
  bool _weaponPowerupActive;
  unsigned long _weaponPowerupEnd;

  // Wave Text
  String _waveText;
  bool _showWaveText;
  int _waveTextTimer;

  int _shopScroll;
  int _lastTouchX;
  bool _wasTouching;
  int _initialTouchX;

  unsigned long _lastClickTime;
  bool _clickDebounce;

  // Joystick navigation
  int _selectedMenuItem;    // 0=Play, 1=Shop, 2=Exit
  int _selectedShopItem;    // 0-NUM_SKINS for shop items
  int _selectedPauseOption; // 0=Resume, 1=Menu
  unsigned long _lastJoystickTime;
  int _lastJoystickDir;

  // Starfield
  struct Star {
    float x, y, speed;
    uint16_t color;
  };
  std::vector<Star> _stars;

  void updatePlayer(float dt);
  void updateEnemies(float dt);
  void updateBullets(float dt);
  void updateParticles(float dt);
  void updatePowerups(float dt);
  void updateBoss(float dt);
  void checkCollisions();
  void spawnEnemy();
  void spawnEnemyWave();
  void spawnFormation(int type); // New formation spawner
  void spawnPowerup();
  void spawnBoss();
  void createExplosion(float x, float y, uint16_t color);
  int getDifficultyLevel();

  // Nuevas funciones para tienda y skins
  void drawShop(int offsetY);
  void drawSkinPreview(int centerX, int centerY, int skinId);
  void drawWinScreen(int offsetY);
  void purchaseSkin(int skinId);
  void equipSkin(int skinId);
  void saveGameData();
  void loadGameData();
  void returnToMainMenu();

  // Graphics helpers
  void drawPlayer(int offsetY);
  void drawEnemy(Entity &e, int offsetY);
  void drawBullet(Entity &e, int offsetY);
  void drawParticles(int offsetY);
  void drawPowerup(Entity &p, int offsetY);
  void drawBoss(int offsetY);
  void drawExplosion(Entity &p, int offsetY);
  void drawHUD(int offsetY);
  void drawMenu(int offsetY);
  void drawPauseMenu(int offsetY);
  void drawGameOver(int offsetY);
  uint16_t getSkinColor(int skinId);
};

#endif