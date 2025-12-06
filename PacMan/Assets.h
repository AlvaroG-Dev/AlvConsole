#ifndef ASSETS_H
#define ASSETS_H

#include <Arduino.h>

// Color definitions
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED 0xF800
#define C_GREEN 0x07E0
#define C_BLUE 0x001F
#define C_CYAN 0x07FF
#define C_MAGENTA 0xF81F
#define C_YELLOW 0xFFE0
#define C_ORANGE 0xFD20
#define C_PINK 0xFC9F
#define C_GRAY 0x8410
#define C_DKGRAY 0x4208

// Game-specific colors
#define C_TRSP 0xFFFF // Transparent (placeholder)
#define C_BLACK 0x0000
#define C_WHIT 0xFFFF
#define C_RED 0xF800
#define C_GREN 0x07E0
#define C_BLUE 0x001F
#define C_CYAN 0x07FF
#define C_YELL 0xFFE0
#define C_ORNG 0xFD20
#define C_PURP 0x8010
#define C_PINK 0xFC9F
#define C_GREY 0x8410
#define C_DKGR 0x2104

// Placeholder sprite arrays - replace with your actual bitmaps
// Pac-Man sprites (16x16)
const uint16_t pacman_anim1[16 * 16] = {
    // Placeholder - replace with your Pac-Man sprite data
    C_TRSP, C_TRSP, C_YELL, C_YELL, C_YELL, C_YELL, C_YELL, C_YELL,
    C_YELL, C_YELL, C_YELL, C_YELL, C_YELL, C_TRSP, C_TRSP, C_TRSP,
    // ... complete with your actual sprite data
};

const uint16_t pacman_anim2[16 * 16] = {
    // Placeholder for second animation frame
};

// Ghost sprites (16x16)
const uint16_t ghost_blinky[16 * 16] = {
    // Red ghost sprite data
};

const uint16_t ghost_pinky[16 * 16] = {
    // Pink ghost sprite data
};

const uint16_t ghost_inky[16 * 16] = {
    // Cyan ghost sprite data
};

const uint16_t ghost_clyde[16 * 16] = {
    // Orange ghost sprite data
};

const uint16_t ghost_frightened[16 * 16] = {
    // Blue frightened ghost sprite data
};

const uint16_t ghost_eyes[16 * 16] = {
    // Ghost eyes sprite data
};

// Maze elements
const uint16_t wall_corner[16 * 16] = {
    // Maze corner sprite
};

const uint16_t wall_vertical[16 * 16] = {
    // Vertical wall sprite
};

const uint16_t wall_horizontal[16 * 16] = {
    // Horizontal wall sprite
};

// UI elements
const uint16_t ui_button_normal[40 * 20] = {
    // Button sprite
};

const uint16_t ui_button_pressed[40 * 20] = {
    // Pressed button sprite
};

// Font data (if using custom fonts)
const uint8_t font_8x8[95][8] = {
    // Custom font data if needed
};

#endif