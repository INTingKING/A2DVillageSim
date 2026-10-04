#pragma once
#include "world.hpp"
#include <cstdint>
#include <vector>

// Draws the world into a 32-bit XRGB pixel buffer, 8x8 pixels per tile.
constexpr int TILE_PX = 8;
constexpr int VIEW_W = sim::MAP_W * TILE_PX;   // 512
constexpr int VIEW_H = sim::MAP_H * TILE_PX;   // 960

// What the UI wants drawn on top of the world (placement ghost, road preview, demolish target).
struct Overlay {
    int ghost = -1;            // BType being placed, or -1
    int gx = 0, gy = 0;        // ghost top-left tile
    bool ghostOk = false;
    bool roadMode = false;
    std::vector<int> roadTiles;   // tiles painted in the current drag (preview)
    int demolishX = -1, demolishY = -1;
    int selected = -1;         // building index tapped for info
};

void drawWorld(const sim::World& w, uint32_t* px, int pitchPx, int frame, const Overlay& ov);
