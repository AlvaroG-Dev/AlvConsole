#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "Assets.h"
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
  STATE_WIN,
  STATE_SHOP
};

enum Direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_NONE };

struct Position {
  int x, y;
};

struct Ghost {
  Position pos;
  Position prevPos;
  Position target;
  Direction dir;
  int type;
  bool frightened;
  bool eaten;
  unsigned long deadTime;
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
  TFT_eSprite *_canvas;
  bool _useSprite;

  GameState _state;
  int _score;
  int _highScore;
  int _lives;
  int _level;
  int _dotsEaten;
  int _coins;
  int _totalCoins;

  // Player
  Position _pacman;
  Position _prevPacman;
  Direction _pacmanDir;
  Direction _nextDir;
  float _animFrame;
  bool _mouthOpen;
  float _moveTimer;

  // Ghosts
  std::vector<Ghost> _ghosts;
  float _ghostMoveTimer;

  // Maze (17x13 for 24px tiles)
  static const int MAZE_WIDTH = 17;
  static const int MAZE_HEIGHT = 13;
  int _maze[13][17];
  std::vector<Position> _dots;
  std::vector<Position> _powerPellets;

  // Game timers
  unsigned long _gameStartTime;
  unsigned long _frightenedStart;
  bool _frightenedMode;
  int _frightenedTime;

  // Control
  bool _clickDebounce;
  unsigned long _lastClickTime;

  // Swipe control
  int _touchStartX, _touchStartY;
  bool _isTouching;

  // Shop system
  int _selectedSkin;
  int _selectedTheme;
  bool _ownedSkins[8];  // Increased to 8 skins
  bool _ownedThemes[4]; // 4 themes
  int _shopScrollOffset;
  int _lastJoystickDir;
  unsigned long _lastJoystickTime;
  int _lastTouchY; // Para el scroll
  bool _touchJustStarted;

  // Navegación con joystick
  int _selectedMenuItem;    // 0=Jugar, 1=Tienda, 2=Salir
  int _selectedShopItem;    // 0-11 para items de tienda (8 skins + 4 themes)
  int _selectedPauseOption; // 0=Resume, 1=Menu
  unsigned long _lastJoystickMenuTime;
  bool _joystickMenuMoved;

  void startGame();
  void resetLevel();
  void loadMaze();
  void loadMaze(int level);
  void initializeGhosts();
  float getGhostSpeed();
  float getPacmanSpeed();
  void updatePacman(float dt);
  void updateGhosts(float dt);
  void updateGhost(Ghost &ghost, float dt);
  void checkCollisions();
  void handleInput(Point touch);
  void movePacman();
  bool isValidMove(int x, int y, Direction dir);
  Position getNextPosition(Position pos, Direction dir);
  void eatDot(int x, int y);
  void eatPowerPellet(int x, int y);
  void scareGhosts();
  void respawnPacman();
  void respawnGhosts();
  void nextLevel();
  void gameOver();
  void returnToMenu();
  void drawShop(int offsetY);

  // Drawing functions
  void drawMaze(int offsetY);
  void drawPacman(int offsetY);
  void drawGhosts(int offsetY);
  void drawGhost(const Ghost &ghost, int offsetY);
  void drawHUD(int offsetY);
  void drawMenu(int offsetY);
  void drawPauseMenu(int offsetY);
  void drawGameOver(int offsetY);
  void drawWinScreen(int offsetY);

  // Helper functions
  Direction getOppositeDirection(Direction dir);
  int manhattanDistance(Position a, Position b);
  Position getGhostTarget(int ghostType);
};

#endif