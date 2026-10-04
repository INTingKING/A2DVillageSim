#pragma once
#include "world.hpp"
#include <cstdint>

// Draws the world into a 32-bit XRGB pixel buffer, 4x4 pixels per tile.
constexpr int TILE_PX = 4;
constexpr int VIEW_W = sim::MAP_W * TILE_PX;   // 480
constexpr int VIEW_H = sim::MAP_H * TILE_PX;   // 256
constexpr int HUD_H = 24;
constexpr int SCREEN_W = VIEW_W;
constexpr int SCREEN_H = VIEW_H + HUD_H;        // 280

void drawWorld(const sim::World& w, uint32_t* px, int pitchPx, int frame,
               int cursorX, int cursorY, int powerSel);
