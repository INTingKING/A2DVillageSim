#pragma once
#include "world.hpp"
#include <cstdint>
#include <vector>

// Draws the world into a 32-bit XRGB pixel buffer, 16x16 pixels per tile.
constexpr int TILE_PX = 16;
constexpr int VIEW_W = sim::MAP_W * TILE_PX;   // 1024
constexpr int VIEW_H = sim::MAP_H * TILE_PX;   // 1920

// What the UI wants drawn on top of the world (placement ghost, road preview, demolish target).
struct Overlay {
    int ghost = -1;            // BType being placed, or -1
    int gx = 0, gy = 0;        // ghost top-left tile
    bool ghostOk = false;
    bool roadMode = false;
    std::vector<int> roadTiles;   // tiles painted in the current drag (preview)
    int demolishX = -1, demolishY = -1;
    int selected = -1;         // building index tapped for info
    int markScale = 1;         // need bubbles are drawn this many times bigger (zoomed out)
    // visible part of the world in world pixels; only this region is redrawn
    int vx0 = 0, vy0 = 0, vx1 = VIEW_W, vy1 = VIEW_H;
};

void drawWorld(const sim::World& w, uint32_t* px, int pitchPx, int frame, const Overlay& ov);
