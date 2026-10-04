#pragma once
// A tiny scripted "player" that builds a sensible town. Used by the self-test (balance checks)
// and by `--demo` screenshots. Not part of normal play.
#include "world.hpp"
#include <cmath>
#include <queue>

namespace autoplay {
using namespace sim;

inline int runDays(World& w, int days) {
    for (int t = 0; t < days * TICKS_PER_DAY && !w.gameOver(); t++) w.tick();
    return w.day();
}

// Road from a tile next to the building to the existing road network (BFS over buildable land).
inline bool connect(World& w, int bi) {
    if (w.buildings[bi].connected) return true;
    const Building& b = w.buildings[bi];
    const BInfo& in = binfo(b.type);
    std::vector<int> prev(MAP_W * MAP_H, -2);
    std::queue<int> q;
    for (int y = b.y - 1; y <= b.y + in.h; y++)
        for (int x = b.x - 1; x <= b.x + in.w; x++) {
            bool edge = (y == b.y - 1 || y == b.y + in.h) != (x == b.x - 1 || x == b.x + in.w);
            if (edge && w.inside(x, y) && (w.canRoad(x, y) || w.at(x, y).t == Tile::Road)) { prev[y * MAP_W + x] = -1; q.push(y * MAP_W + x); }
        }
    int hit = -1;
    while (!q.empty() && hit < 0) {
        int i = q.front(); q.pop();
        int x = i % MAP_W, y = i / MAP_W;
        if (w.at(x, y).t == Tile::Road) { hit = i; break; }
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!w.inside(nx, ny) || prev[ny * MAP_W + nx] != -2) continue;
            if (!(w.canRoad(nx, ny) || w.at(nx, ny).t == Tile::Road)) continue;
            prev[ny * MAP_W + nx] = i; q.push(ny * MAP_W + nx);
        }
    }
    if (hit < 0) return false;
    for (int i = hit; i != -1; i = prev[i]) if (w.at(i % MAP_W, i / MAP_W).t != Tile::Road) w.placeRoad(i % MAP_W, i / MAP_W);
    return w.buildings[bi].connected;
}

// Place a building on the closest free spot to the hall (leaving a 1-tile gap for roads).
inline bool build(World& w, BType t) {
    if (!w.canAfford(t)) return false;
    const BInfo& in = binfo(t);
    float best = 1e9f; int bx = -1, by = -1;
    for (int y = 1; y < MAP_H - in.h - 1; y++)
        for (int x = 1; x < MAP_W - in.w - 1; x++) {
            if (!w.canPlace(t, x, y)) continue;
            bool gap = true;   // keep one free ring so roads fit
            for (int yy = y - 1; yy <= y + in.h && gap; yy++)
                for (int xx = x - 1; xx <= x + in.w && gap; xx++)
                    if (w.at(xx, yy).t == Tile::Building) gap = false;
            if (!gap) continue;
            float d = std::hypot(x - (float)w.hallX, y - (float)w.hallY);
            if (t == BType::Lumber) {   // want trees nearby
                int trees = 0;
                for (int yy = y - 5; yy <= y + 6; yy++) for (int xx = x - 5; xx <= x + 6; xx++)
                    if (w.inside(xx, yy) && w.at(xx, yy).t == Tile::Forest) trees++;
                d -= trees * 0.3f;
            }
            if (d < best) { best = d; bx = x; by = y; }
        }
    if (bx < 0 || !w.place(t, bx, by)) return false;
    connect(w, w.buildingAt(bx, by));
    return true;
}

// A simple build order, re-run each day.
inline void player(World& w) {
    auto has = [&](BType t) { return w.count(t); };
    for (int i = 0; i < (int)w.buildings.size(); i++) if (w.buildings[i].alive && !w.buildings[i].connected) connect(w, i);
    if (has(BType::Lumber) < 1) build(w, BType::Lumber);
    if (has(BType::Fisher) < 1 + w.population() / 8) build(w, BType::Fisher);
    if (has(BType::Sawmill) < 1) build(w, BType::Sawmill);
    if (has(BType::Farm) < 1 + w.population() / 10) build(w, BType::Farm);
    if (has(BType::Farm) >= 1 && has(BType::Mill) < 1 + w.population() / 25) build(w, BType::Mill);
    if (has(BType::Mill) >= 1 && has(BType::Bakery) < 1 + w.population() / 20) build(w, BType::Bakery);
    if (w.housing() - w.population() < 3) build(w, BType::House);
    if (has(BType::Lumber) < 2 && w.population() > 10) build(w, BType::Lumber);
}
inline void defender(World& w) {
    player(w);
    if (w.count(BType::Well) < 1 + w.population() / 12) build(w, BType::Well);
    if (w.population() > 8 && w.count(BType::Tower) < 1 + w.population() / 15) build(w, BType::Tower);
    if (w.population() > 6 && w.count(BType::Healer) < 1 + w.population() / 30) build(w, BType::Healer);
}

inline int playRun(World& w, int days, void (*p)(World&)) {
    for (int d = 0; d < days && !w.gameOver(); d++) { p(w); runDays(w, 1); }
    return w.day();
}

} // namespace autoplay
