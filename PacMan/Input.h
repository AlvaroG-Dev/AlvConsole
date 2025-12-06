#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>
#include <Wire.h>

#define FT6336_ADDR 0x38
#define TOUCH_SDA 4
#define TOUCH_SCL 5
#define TOUCH_RST 3
#define TOUCH_INT 2

// Joystick pins
#define JOYSTICK_X_PIN 15
#define JOYSTICK_Y_PIN 16
#define JOYSTICK_DEADZONE 800
#define JOYSTICK_CENTER 2048

#define BUTTON_A_PIN 11
#define BUTTON_B_PIN 12

struct Point {
  int x;
  int y;
  bool touched;
};

// Definir Direction aquí para evitar dependencias circulares
enum InputDirection {
  INPUT_DIR_UP,
  INPUT_DIR_DOWN,
  INPUT_DIR_LEFT,
  INPUT_DIR_RIGHT,
  INPUT_DIR_NONE
};

struct JoystickInput {
  int x;
  int y;
  InputDirection direction;
  bool active;
};

struct ButtonInput {
  bool aPressed;
  bool bPressed;
  bool aJustPressed;
  bool bJustPressed;
};

class Input {
public:
  void begin() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(100);

    // Inicializar pines del joystick
    pinMode(JOYSTICK_X_PIN, INPUT);
    pinMode(JOYSTICK_Y_PIN, INPUT);

    // Inicializar botones con pull-up interno
    pinMode(BUTTON_A_PIN, INPUT_PULLUP);
    pinMode(BUTTON_B_PIN, INPUT_PULLUP);

    _lastAState = false;
    _lastBState = false;
  }
  ButtonInput getButtons() {
    ButtonInput btn;

    // Los botones están activos en LOW (pull-up)
    bool aState = !digitalRead(BUTTON_A_PIN);
    bool bState = !digitalRead(BUTTON_B_PIN);

    btn.aPressed = aState;
    btn.bPressed = bState;

    // Detectar flanco de subida (justo presionado)
    btn.aJustPressed = aState && !_lastAState;
    btn.bJustPressed = bState && !_lastBState;

    _lastAState = aState;
    _lastBState = bState;

    return btn;
  }
  Point getTouch(int screenWidth, int screenHeight) {
    Point p = {0, 0, false};

    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() != 0)
      return p;

    Wire.requestFrom(FT6336_ADDR, 1);
    if (Wire.available() < 1)
      return p;

    uint8_t touches = Wire.read() & 0x0F;
    if (touches == 0)
      return p;

    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0x03);
    if (Wire.endTransmission() != 0)
      return p;

    Wire.requestFrom(FT6336_ADDR, 4);
    if (Wire.available() < 4)
      return p;

    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    uint16_t raw_x = ((xh & 0x0F) << 8) | xl;
    uint16_t raw_y = ((yh & 0x0F) << 8) | yl;

    p.x = screenWidth - raw_y;
    p.y = raw_x;

    p.x = constrain(p.x, 0, screenWidth - 1);
    p.y = constrain(p.y, 0, screenHeight - 1);
    p.touched = true;

    return p;
  }

  JoystickInput getJoystick() {
    JoystickInput joy = {0, 0, INPUT_DIR_NONE, false};

    int rawX = analogRead(JOYSTICK_X_PIN);
    int rawY = analogRead(JOYSTICK_Y_PIN);

    joy.x = rawX - JOYSTICK_CENTER;
    joy.y = rawY - JOYSTICK_CENTER;

    // Aplicar zona muerta
    if (abs(joy.x) < JOYSTICK_DEADZONE && abs(joy.y) < JOYSTICK_DEADZONE) {
      return joy;
    }

    joy.active = true;

    // Determinar dirección dominante
    if (abs(joy.x) > abs(joy.y)) {
      joy.direction = (joy.x > 0) ? INPUT_DIR_RIGHT : INPUT_DIR_LEFT;
    } else {
      joy.direction = (joy.y > 0) ? INPUT_DIR_DOWN : INPUT_DIR_UP;
    }

    return joy;
  }

  Input() : _lastAState(false), _lastBState(false) {}

private:
  bool _lastAState;
  bool _lastBState;
};

#endif