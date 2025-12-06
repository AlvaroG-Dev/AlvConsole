#ifndef ASSETS_H
#define ASSETS_H

#include <Arduino.h>

// Color definitions (RGB565 format)
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED 0xF800
#define C_GREEN 0x07E0
#define C_BLUE 0x001F
#define C_CYAN 0x07FF
#define C_MAGENTA 0xF81F
#define C_YELLOW 0xFFE0
#define C_ORANGE 0xFD20
#define C_SKYBLUE 0x867D
#define C_GRASS 0x2D05     // Dark green grass
#define C_DARKGREEN 0x0320 // Darker green
#define C_LIGHTGRAY 0xC618 // Light gray

// Transparency color
#define C_TRSP 0x0001

// ==========================================
// PLACEHOLDER SPRITES
// Replace the content of these arrays with your converted images
// Recommended tool: http://www.rinkydinkelectronics.com/t_imageconverter565.php
// ==========================================

// Ball Sprite (16x16)
const uint16_t ball_sprite[16 * 16] = {
    // Placeholder: White circle
    C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,
    C_TRSP,  C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,
    C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,
    C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,
    C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_BLACK, C_BLACK, C_WHITE,
    C_WHITE, C_BLACK, C_BLACK, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_BLACK, C_BLACK, C_WHITE,
    C_WHITE, C_BLACK, C_BLACK, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_BLACK, C_BLACK, C_WHITE,
    C_WHITE, C_BLACK, C_BLACK, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_BLACK, C_BLACK, C_WHITE,
    C_WHITE, C_BLACK, C_BLACK, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,
    C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,
    C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,
    C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,
    C_TRSP,  C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,
    C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_WHITE, C_WHITE, C_WHITE,
    C_WHITE, C_WHITE, C_WHITE, C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP,  C_TRSP};

// Goalkeeper Idle (32x32)
const uint16_t keeper_idle[32 * 32] = {
    // Placeholder: Red Box
    // Fill with C_RED or your sprite data
};

// Goalkeeper Dive Left (48x32)
const uint16_t keeper_dive_left[48 * 32] = {
    // Placeholder
};

// Goalkeeper Dive Right (48x32)
const uint16_t keeper_dive_right[48 * 32] = {
    // Placeholder
};

// Player Idle (32x32)
const uint16_t player_idle[32 * 32] = {
    // Placeholder: Blue Box
};

// Player Kick (32x32)
const uint16_t player_kick[32 * 32] = {
    // Placeholder
};

#endif
