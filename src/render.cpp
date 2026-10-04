#include "render.hpp"
#include <algorithm>
#include <cmath>

using namespace sim;

namespace {
// Resurrect 64 palette (Lospec) subset
constexpr uint32_t DEEP = 0x323353, WATER = 0x4d65b4, WATER_HI = 0x4d9be6, FOAM = 0x8fd3ff;
constexpr uint32_t SAND = 0xe6904e, SAND_D = 0xcd683d;
constexpr uint32_t GRASS = 0x239063, GRASS_D = 0x165a4c, GRASS_L = 0x1ebc73;
constexpr uint32_t TREE = 0x165a4c, TREE_L = 0x239063, TRUNK = 0x4c3e24;
constexpr uint32_t ROCK = 0x625565, ROCK_L = 0x9babb2, SNOW = 0xc7dcd0;
constexpr uint32_t SOIL = 0x694f62, SOIL_D = 0x45293f, SPROUT = 0x91db69, RIPE = 0xf9c22b;
constexpr uint32_t ROOF = 0xb33831, ROOF_D = 0x6e2727, WALL = 0xab947a, DOOR = 0x4c3e24, LIT = 0xfbff86;
constexpr uint32_t HALL_ROOF = 0x6b3e75, FLAG = 0xf9c22b;
constexpr uint32_t ASH = 0x313638, ASH_L = 0x3e3546;
constexpr uint32_t FIRE1 = 0xfb6b1d, FIRE2 = 0xf9c22b, FIRE3 = 0xea4f36;
constexpr uint32_t SKIN = 0xfdcbb0, SICK = 0xa2a947;
constexpr uint32_t JOB_FARM = 0xf79617, JOB_WOOD = 0x8fd3ff, JOB_BUILD = 0xffffff, RAIDER = 0xe83b3b, RAIDER_D = 0x2e222f;
constexpr uint32_t CURSOR = 0xffffff;

uint32_t hashc(int x, int y) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    int r = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
    int g = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
    int bl = (int)((a & 255) * (1 - t) + (b & 255) * t);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

struct Canvas {
    uint32_t* px; int pitch;
    void put(int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= VIEW_W || y >= VIEW_H) return;
        px[y * pitch + x] = c;
    }
};

void drawTile(Canvas& cv, const World& w, int tx, int ty, int frame, bool winter) {
    const Cell& c = w.at(tx, ty);
    int ox = tx * TILE_PX, oy = ty * TILE_PX;
    uint32_t h = hashc(tx, ty);
    auto fill = [&](uint32_t col) { for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) cv.put(ox + x, oy + y, col); };
    auto grass = [&]() {
        fill(winter ? mixc(GRASS, SNOW, 0.75f) : GRASS);
        cv.put(ox + (h & 3), oy + ((h >> 2) & 3), winter ? SNOW : GRASS_D);
        if ((h >> 4) & 1) cv.put(ox + ((h >> 5) & 3), oy + ((h >> 7) & 3), winter ? 0xffffff : GRASS_L);
    };
    switch (c.t) {
    case Tile::Deep:
        fill(DEEP);
        if (((h >> 3) + frame / 20) % 9 == 0) cv.put(ox + (h & 3), oy + 1, WATER);
        break;
    case Tile::Water:
        fill(WATER);
        if (((h >> 3) + frame / 15) % 6 == 0) { cv.put(ox + (h & 3), oy + 2, WATER_HI); cv.put(ox + ((h & 3) + 1) % 4, oy + 2, WATER_HI); }
        // foam next to land
        for (int k = 0; k < 4; k++) {
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            int nx = tx + dx[k], ny = ty + dy[k];
            if (!w.inside(nx, ny)) continue;
            Tile t = w.at(nx, ny).t;
            if (t != Tile::Water && t != Tile::Deep && (frame / 25 + h) % 3) {
                if (dx[k] == 1) cv.put(ox + 3, oy + (h & 3), FOAM);
                if (dx[k] == -1) cv.put(ox, oy + ((h >> 2) & 3), FOAM);
                if (dy[k] == 1) cv.put(ox + ((h >> 4) & 3), oy + 3, FOAM);
                if (dy[k] == -1) cv.put(ox + ((h >> 6) & 3), oy, FOAM);
            }
        }
        break;
    case Tile::Sand:
        fill(SAND); cv.put(ox + (h & 3), oy + ((h >> 2) & 3), SAND_D);
        break;
    case Tile::Grass: grass(); break;
    case Tile::Forest: {
        grass();
        bool big = c.res > 1.5f;
        uint32_t canopy = winter ? mixc(TREE, SNOW, 0.35f) : TREE;
        uint32_t lit = winter ? SNOW : TREE_L;
        if (big) {
            for (int x = 0; x < 3; x++) cv.put(ox + x, oy + 1, canopy);
            cv.put(ox + 1, oy, lit); cv.put(ox + 0, oy + 0, canopy); cv.put(ox + 2, oy, canopy);
            cv.put(ox + 1, oy + 2, TRUNK);
        } else {
            cv.put(ox + 1, oy + 1, lit); cv.put(ox + 2, oy + 1, canopy);
            cv.put(ox + 1, oy + 2, canopy); cv.put(ox + 2, oy + 2, canopy); cv.put(ox + 1, oy + 3, TRUNK);
        }
        break;
    }
    case Tile::Mountain:
        fill(ROCK);
        cv.put(ox + 1, oy, ROCK_L); cv.put(ox + 2, oy, ROCK_L); cv.put(ox + 1, oy + 1, ROCK_L);
        if (c.height > 0.7f || winter) { cv.put(ox + 1, oy, SNOW); cv.put(ox + 2, oy, SNOW); }
        cv.put(ox + 3, oy + 3, 0x484a77);
        break;
    case Tile::Farm: {
        fill(winter ? mixc(SOIL, SNOW, 0.6f) : SOIL);
        uint32_t crop = c.res >= 0.95f ? RIPE : (c.res > 0.4f ? SPROUT : SOIL_D);
        for (int x = 0; x < 4; x++) { cv.put(ox + x, oy + 1, crop); cv.put(ox + x, oy + 3, crop); }
        if (c.res < 0.4f) { cv.put(ox + 1, oy + 1, SPROUT); cv.put(ox + 2, oy + 3, SPROUT); }
        break;
    }
    case Tile::House: case Tile::Hall: {
        grass();
        bool hall = c.t == Tile::Hall;
        uint32_t roof = hall ? HALL_ROOF : ROOF;
        cv.put(ox + 1, oy, roof); cv.put(ox + 2, oy, roof);
        for (int x = 0; x < 4; x++) cv.put(ox + x, oy + 1, x == 3 ? ROOF_D : roof);
        for (int x = 0; x < 4; x++) { cv.put(ox + x, oy + 2, WALL); cv.put(ox + x, oy + 3, WALL); }
        cv.put(ox + 1, oy + 3, DOOR);
        float t = w.dayFrac();
        bool night = t > 0.72f || t < 0.12f;
        cv.put(ox + 2, oy + 2, night ? LIT : 0x7f708a);
        if (winter) { cv.put(ox + 1, oy, SNOW); cv.put(ox + 2, oy, SNOW); }
        if (hall) { cv.put(ox + 3, oy, (frame / 10) % 2 ? FLAG : 0xfbb954); }
        break;
    }
    case Tile::Ash:
        fill(ASH); cv.put(ox + (h & 3), oy + ((h >> 2) & 3), ASH_L);
        if (c.res < 1.5f) cv.put(ox + ((h >> 4) & 3), oy + ((h >> 6) & 3), GRASS_D);
        break;
    }
    if (c.fire > 0.f) {
        int f = frame / 4 + (int)h;
        for (int i = 0; i < 5; i++) {
            int x = (h >> (i * 3)) & 3, y = ((h >> (i * 3 + 2)) + f) & 3;
            cv.put(ox + x, oy + y, i % 3 == 0 ? FIRE1 : (i % 3 == 1 ? FIRE2 : FIRE3));
        }
    }
}

void drawPerson(Canvas& cv, float x, float y, uint32_t body, uint32_t head, int frame, bool moving) {
    int px = (int)std::lround(x * TILE_PX) + 1, py = (int)std::lround(y * TILE_PX);
    int bob = moving && (frame / 6) % 2 ? 1 : 0;
    cv.put(px, py - 1 - bob, head);
    cv.put(px, py - bob, body);
    cv.put(px, py + 1, moving && (frame / 6) % 2 ? body : 0x2e222f);
    cv.put(px + 1, py - bob, mixc(body, 0x000000, 0.35f));
}

void circle(Canvas& cv, int cx, int cy, float r, uint32_t col, int frame) {
    int steps = std::max(16, (int)(r * 6));
    for (int i = 0; i < steps; i++) {
        if ((i + frame / 3) % 4 == 0) continue; // marching ants
        float a = i * 6.2831853f / steps;
        cv.put(cx + (int)std::lround(std::cos(a) * r), cy + (int)std::lround(std::sin(a) * r), col);
    }
}
} // namespace

void drawWorld(const World& w, uint32_t* px, int pitchPx, int frame,
               int cursorX, int cursorY, int powerSel) {
    Canvas cv{px, pitchPx};
    bool winter = w.season() == Season::Winter;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) drawTile(cv, w, tx, ty, frame, winter);

    for (const Villager& v : w.villagers) {
        if (!v.alive) continue;
        uint32_t body = v.job == Job::Farmer ? JOB_FARM : (v.job == Job::Lumber ? JOB_WOOD : JOB_BUILD);
        bool moving = v.pathPos < v.path.size();
        drawPerson(cv, v.x, v.y, v.sick ? SICK : body, v.sick ? SICK : SKIN, frame, moving);
        if (v.carrying) cv.put((int)std::lround(v.x * TILE_PX) + 2, (int)std::lround(v.y * TILE_PX) - 2,
                               v.carryKind == 0 ? RIPE : TRUNK);
    }
    for (const Raider& r : w.raiders)
        if (r.alive) drawPerson(cv, r.x, r.y, RAIDER, RAIDER_D, frame, true);

    // day/night + drought tint
    float t = w.dayFrac();
    float night = 0.f;
    if (t > 0.70f) night = std::min(1.f, (t - 0.70f) / 0.12f);
    else if (t < 0.12f) night = 1.f - t / 0.12f;
    night *= 0.55f;
    float dry = w.droughtDays > 0 ? 0.12f : 0.f;
    if (night > 0.f || dry > 0.f) {
        for (int y = 0; y < VIEW_H; y++)
            for (int x = 0; x < VIEW_W; x++) {
                uint32_t& p = px[y * pitchPx + x];
                if (p == LIT || p == FIRE1 || p == FIRE2 || p == FIRE3) continue; // lights glow through
                uint32_t c = p;
                if (dry > 0.f) c = mixc(c, 0xfbb954, dry);
                if (night > 0.f) c = mixc(c, 0x1a1b33, night);
                p = c;
            }
    }

    // power effect flash
    int since = w.ticks - w.lastCastTick;
    if (since >= 0 && since < 12 && w.lastCastX >= 0) {
        const PowerInfo& pi = powerInfo(w.lastPower);
        uint32_t col = w.lastPower == Power::Rain ? FOAM : w.lastPower == Power::Heal ? SPROUT
                     : w.lastPower == Power::Bless ? RIPE : 0xffffff;
        int cx = w.lastCastX * TILE_PX + 2, cy = w.lastCastY * TILE_PX + 2;
        float rr = pi.radius * TILE_PX * (0.4f + since / 12.f);
        circle(cv, cx, cy, rr, col, 0);
        if (w.lastPower == Power::Smite && since < 4)
            for (int y = 0; y < cy; y++) cv.put(cx + ((y / 5) % 3) - 1, y, 0xffffff);
    }

    // cursor ring
    if (cursorX >= 0 && cursorY >= 0 && cursorY < VIEW_H && powerSel >= 0) {
        const PowerInfo& pi = powerInfo((Power)powerSel);
        uint32_t col = w.mana >= pi.cost ? CURSOR : 0xe83b3b;
        circle(cv, cursorX, cursorY, pi.radius * TILE_PX, col, frame);
    }
}
