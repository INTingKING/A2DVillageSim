#include "render.hpp"
#include <algorithm>
#include <cmath>

using namespace sim;

namespace {
// Resurrect 64 palette (Lospec) subset
constexpr uint32_t DEEP = 0x323353, WATER = 0x4d65b4, WATER_HI = 0x4d9be6, FOAM = 0x8fd3ff;
constexpr uint32_t SAND = 0xe6904e, SAND_D = 0xcd683d;
constexpr uint32_t GRASS = 0x239063, GRASS_D = 0x165a4c, GRASS_L = 0x1ebc73;
constexpr uint32_t TREE = 0x165a4c, TREE_L = 0x239063, TREE_D = 0x0b3d33, TRUNK = 0x4c3e24;
constexpr uint32_t ROCK = 0x625565, ROCK_L = 0x9babb2, ROCK_D = 0x3e3546, SNOW = 0xc7dcd0;
constexpr uint32_t SOIL = 0x694f62, SOIL_D = 0x45293f, SPROUT = 0x91db69, RIPE = 0xf9c22b;
constexpr uint32_t ROAD = 0xa77b5b, ROAD_D = 0x80553e, ROAD_L = 0xc09473;
constexpr uint32_t WALL = 0xd5c3a2, WALL_D = 0xab947a, DOOR = 0x4c3e24, LIT = 0xfbff86, WINDOW = 0x484a77;
constexpr uint32_t ASH = 0x313638, ASH_L = 0x3e3546;
constexpr uint32_t FIRE1 = 0xfb6b1d, FIRE2 = 0xf9c22b, FIRE3 = 0xea4f36;
constexpr uint32_t SKIN = 0xfdcbb0, SICK = 0xa2a947, CLOTH = 0x4d9be6, RAIDER = 0xe83b3b, RAIDER_D = 0x2e222f;
constexpr uint32_t SHADOW = 0x1b2a2a;
// Roof colours by category (Designer): homes red-brown, food straw, wood green-brown, safety slate.
constexpr uint32_t ROOF_HOME = 0xb33831, ROOF_FOOD = 0xe0a83a, ROOF_WOOD = 0x5b6b2e, ROOF_SAFE = 0x5a6e9c, ROOF_HALL = 0x6b3e75;

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
uint32_t grey(uint32_t c, float t) {   // desaturate
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    int l = (r * 3 + g * 6 + b) / 10;
    return mixc(c, (uint32_t)(l << 16 | l << 8 | l), t);
}
uint32_t lighten(uint32_t c) { return mixc(c, 0xffffff, 0.3f); }
uint32_t darken(uint32_t c) { return mixc(c, 0x000000, 0.35f); }

struct Canvas {
    uint32_t* px; int pitch;
    float desat = 0.f;   // greys out whatever is drawn (idle buildings)
    void put(int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= VIEW_W || y >= VIEW_H) return;
        px[y * pitch + x] = desat > 0.f ? grey(c, desat) : c;
    }
    void rect(int x, int y, int w, int h, uint32_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, c); }
    void blend(int x, int y, uint32_t c, float t) {
        if (x < 0 || y < 0 || x >= VIEW_W || y >= VIEW_H) return;
        uint32_t& p = px[y * pitch + x]; p = mixc(p, c, t);
    }
    void line(float x0, float y0, float x1, float y1, uint32_t c) {
        int n = (int)std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
        for (int i = 0; i <= n; i++) { float t = (float)i / n; put((int)std::lround(x0 + (x1 - x0) * t), (int)std::lround(y0 + (y1 - y0) * t), c); }
    }
};

bool isRoadish(const World& w, int x, int y) {
    if (!w.inside(x, y)) return false;
    return w.at(x, y).t == Tile::Road;
}

void drawGrass(Canvas& cv, int ox, int oy, uint32_t h, bool winter) {
    cv.rect(ox, oy, 8, 8, winter ? mixc(GRASS, SNOW, 0.75f) : GRASS);
    for (int i = 0; i < 3; i++) {
        uint32_t k = h >> (i * 6);
        cv.put(ox + (k & 7), oy + ((k >> 3) & 7), i == 2 ? (winter ? 0xffffff : GRASS_L) : (winter ? SNOW : GRASS_D));
    }
}

void drawTile(Canvas& cv, const World& w, int tx, int ty, int frame, bool winter) {
    const Cell& c = w.at(tx, ty);
    int ox = tx * TILE_PX, oy = ty * TILE_PX;
    uint32_t h = hashc(tx, ty);
    switch (c.t) {
    case Tile::Deep:
        cv.rect(ox, oy, 8, 8, DEEP);
        if (((h >> 3) + frame / 20) % 9 == 0) { cv.put(ox + (h & 7), oy + 3, WATER); cv.put(ox + ((h & 7) + 1) % 8, oy + 3, WATER); }
        break;
    case Tile::Water: {
        cv.rect(ox, oy, 8, 8, WATER);
        if (((h >> 3) + frame / 15) % 6 == 0) for (int i = 0; i < 3; i++) cv.put(ox + ((h & 7) + i) % 8, oy + 4, WATER_HI);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = tx + dx[k], ny = ty + dy[k];
            if (!w.inside(nx, ny)) continue;
            Tile t = w.at(nx, ny).t;
            if (t == Tile::Water || t == Tile::Deep) continue;
            for (int i = 0; i < 8; i++) {
                if ((i + frame / 12 + h) % 3 == 0) continue;
                if (dx[k] == 1) cv.put(ox + 7, oy + i, FOAM);
                if (dx[k] == -1) cv.put(ox, oy + i, FOAM);
                if (dy[k] == 1) cv.put(ox + i, oy + 7, FOAM);
                if (dy[k] == -1) cv.put(ox + i, oy, FOAM);
            }
        }
        break;
    }
    case Tile::Sand:
        cv.rect(ox, oy, 8, 8, winter ? mixc(SAND, SNOW, 0.5f) : SAND);
        cv.put(ox + (h & 7), oy + ((h >> 3) & 7), SAND_D); cv.put(ox + ((h >> 6) & 7), oy + ((h >> 9) & 7), SAND_D);
        break;
    case Tile::Grass: case Tile::Building: drawGrass(cv, ox, oy, h, winter); break;
    case Tile::Forest: {
        drawGrass(cv, ox, oy, h, winter);
        uint32_t canopy = winter ? mixc(TREE, SNOW, 0.35f) : TREE, lit = winter ? SNOW : TREE_L, dk = TREE_D;
        int big = c.res > 1.5f ? 1 : 0, sx = ox + 1 + (h & 1), sy = oy + (big ? 0 : 2);
        // pine: stacked triangles, lit top-left, shadow bottom-right
        cv.put(sx + 3, oy + 7, SHADOW); cv.put(sx + 4, oy + 7, SHADOW);
        cv.put(sx + 2, oy + 6, TRUNK); cv.put(sx + 2, oy + 7, TRUNK);
        int rows = big ? 6 : 4;
        for (int r = 0; r < rows; r++) {
            int half = (r + 1) / 2 + (big ? 0 : 0);
            for (int x = -half; x <= half; x++) {
                uint32_t col = x < 0 ? lit : (x == half && half > 0 ? dk : canopy);
                cv.put(sx + 2 + x, sy + r, col);
            }
        }
        break;
    }
    case Tile::Mountain:
        cv.rect(ox, oy, 8, 8, ROCK);
        for (int i = 0; i < 4; i++) { cv.put(ox + 3 - i / 2 + i, oy + 1 + i / 2, ROCK_L); }
        cv.rect(ox + 2, oy + 1, 3, 2, ROCK_L);
        if (c.height > 0.7f || winter) cv.rect(ox + 3, oy, 2, 2, SNOW);
        for (int i = 0; i < 8; i++) cv.put(ox + i, oy + 7, ROCK_D);
        cv.rect(ox + 6, oy + 4, 2, 3, ROCK_D);
        break;
    case Tile::Road: {
        drawGrass(cv, ox, oy, h, winter);
        uint32_t rc = winter ? mixc(ROAD, SNOW, 0.4f) : ROAD;
        cv.rect(ox + 1, oy + 1, 6, 6, rc);
        bool n = isRoadish(w, tx, ty - 1), s = isRoadish(w, tx, ty + 1), e = isRoadish(w, tx + 1, ty), wv = isRoadish(w, tx - 1, ty);
        if (n) cv.rect(ox + 1, oy, 6, 1, rc);
        if (s) cv.rect(ox + 1, oy + 7, 6, 1, rc);
        if (e) cv.rect(ox + 7, oy + 1, 1, 6, rc);
        if (wv) cv.rect(ox, oy + 1, 1, 6, rc);
        if (n && e) cv.put(ox + 7, oy, rc);
        if (n && wv) cv.put(ox, oy, rc);
        if (s && e) cv.put(ox + 7, oy + 7, rc);
        if (s && wv) cv.put(ox, oy + 7, rc);
        if (!s) cv.rect(ox + 1, oy + 7, 6, 1, ROAD_D);
        if (!e) cv.rect(ox + 7, oy + 1, 1, 6, ROAD_D);
        cv.put(ox + 2 + (h & 3), oy + 2 + ((h >> 2) & 3), ROAD_L);
        cv.put(ox + 2 + ((h >> 4) & 3), oy + 2 + ((h >> 6) & 3), ROAD_D);
        break;
    }
    case Tile::Ash:
        cv.rect(ox, oy, 8, 8, ASH);
        cv.put(ox + (h & 7), oy + ((h >> 3) & 7), ASH_L); cv.put(ox + ((h >> 6) & 7), oy + ((h >> 9) & 7), ASH_L);
        if (c.res < 1.5f) { cv.put(ox + ((h >> 12) & 7), oy + ((h >> 15) & 7), GRASS_D); cv.put(ox + ((h >> 18) & 7), oy + ((h >> 21) & 7), GRASS); }
        break;
    }
}

void drawFire(Canvas& cv, int ox, int oy, uint32_t h, int frame) {
    int f = frame / 3 + (int)h;
    for (int i = 0; i < 9; i++) {
        int x = (h >> (i * 3)) & 7, y = 7 - ((((h >> (i * 2)) & 3) + f + i) % 7);
        cv.put(ox + x, oy + y, i % 3 == 0 ? FIRE1 : (i % 3 == 1 ? FIRE2 : FIRE3));
    }
}

uint32_t roofOf(BType t) {
    if (t == BType::Hall) return ROOF_HALL;
    switch (binfo(t).cat) {
    case Category::Home: return ROOF_HOME;
    case Category::Food: return ROOF_FOOD;
    case Category::Wood: return ROOF_WOOD;
    default: return ROOF_SAFE;
    }
}

// A cottage filling (x,y,w,h) pixels: pitched roof on top, wall below, door + window.
void cottage(Canvas& cv, int x, int y, int w, int h, uint32_t roof, bool night, bool winter, int roofH = -1) {
    if (roofH < 0) roofH = h / 2 + 1;
    // drop shadow (bottom-right)
    cv.rect(x + 1, y + h, w, 1, SHADOW); cv.rect(x + w, y + 2, 1, h - 1, SHADOW);
    // wall
    int wy = y + roofH;
    cv.rect(x + 1, wy, w - 2, h - roofH, WALL);
    cv.rect(x + w - 2, wy, 1, h - roofH, WALL_D);
    // roof: rows get wider toward the bottom
    for (int r = 0; r < roofH; r++) {
        int inset = std::max(0, (roofH - 1 - r) * w / (roofH * 3));
        for (int i = inset; i < w - inset; i++) {
            uint32_t c = roof;
            if (i == inset || r == 0) c = lighten(roof);
            else if (i == w - inset - 1 || r == roofH - 1) c = darken(roof);
            if (winter && r < roofH / 2 && i > inset && i < w - inset - 1) c = SNOW;
            cv.put(x + i, y + r, c);
        }
    }
    // door + window
    int dx = x + w / 2 - 1;
    cv.rect(dx, y + h - 3, 2, 3, DOOR);
    if (w >= 10) cv.rect(x + 2, wy + 1, 2, 2, night ? LIT : WINDOW);
    if (w >= 12) cv.rect(x + w - 5, wy + 1, 2, 2, night ? LIT : WINDOW);
}

void drawBuilding(Canvas& cv, const World& w, const Building& b, int frame, bool night, bool winter) {
    const BInfo& in = binfo(b.type);
    int x = b.x * TILE_PX, y = b.y * TILE_PX, W = in.w * TILE_PX, H = in.h * TILE_PX;
    uint32_t roof = roofOf(b.type);
    switch (b.type) {
    case BType::Hall: {
        cottage(cv, x + 1, y + 6, W - 3, H - 8, roof, night, winter, 9);
        // bell tower + flag
        cv.rect(x + W / 2 - 2, y + 1, 4, 6, WALL_D); cv.rect(x + W / 2 - 1, y + 2, 2, 2, DOOR);
        cv.put(x + W / 2, y, 0x4c3e24);
        uint32_t flag = (frame / 10) % 2 ? 0xf9c22b : 0xfbb954;
        cv.rect(x + W / 2 + 1, y, 3, 2, flag);
        break;
    }
    case BType::House: {
        // higher-tier look later; for now one cottage, with a chimney
        cottage(cv, x + 1, y + 2, W - 2, H - 3, roof, night, winter);
        cv.rect(x + W - 5, y, 2, 3, ROCK_D);
        if (b.cold && (frame / 8) % 2 == 0) {} else if (!b.cold) cv.put(x + W - 4 + ((frame / 10) % 2), y - 1 - (frame / 5) % 3, 0xc7dcd0);
        break;
    }
    case BType::Lumber: {
        cottage(cv, x + 1, y + 1, W - 6, H - 3, roof, night, winter);
        // log pile (silhouette)
        for (int i = 0; i < 3; i++) { cv.rect(x + W - 6, y + H - 4 - i * 2, 5, 2, TRUNK); cv.put(x + W - 6, y + H - 4 - i * 2, 0xc09473); }
        cv.put(x + W - 2, y + 3, ROCK_L); cv.rect(x + W - 2, y + 4, 1, 4, TRUNK);   // axe in stump
        break;
    }
    case BType::Sawmill: {
        cottage(cv, x + 1, y + 3, W - 2, H - 4, roof, night, winter, 5);
        // round saw blade spinning on the roof edge
        int cx = x + W - 4, cy = y + 3;
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785f + frame * 0.4f;
            cv.put(cx + (int)std::lround(std::cos(a) * 2.5f), cy + (int)std::lround(std::sin(a) * 2.5f), ROCK_L);
        }
        cv.put(cx, cy, ROCK_D);
        cv.rect(x + 1, y + H - 3, 4, 2, 0xc09473);   // planks
        break;
    }
    case BType::Fisher: {
        cottage(cv, x + 1, y + 2, W - 4, H - 3, roof, night, winter);
        // net rack + fish
        cv.rect(x + W - 3, y + 2, 1, H - 4, TRUNK);
        for (int j = 0; j < 4; j++) cv.put(x + W - 2, y + 4 + j * 2, FOAM);
        cv.rect(x + W - 2, y + H - 3, 2, 1, 0x9babb2);
        break;
    }
    case BType::Farm: {
        // fenced field; crop rows show growth
        float g = b.grow;
        uint32_t crop = g >= 0.99f ? RIPE : (g > 0.5f ? mixc(SPROUT, RIPE, (g - 0.5f) * 1.6f) : SPROUT);
        uint32_t soil = winter ? mixc(SOIL, SNOW, 0.6f) : SOIL;
        cv.rect(x + 1, y + 1, W - 2, H - 2, soil);
        for (int r = y + 3; r < y + H - 2; r += 3) {
            cv.rect(x + 2, r, W - 4, 1, SOIL_D);
            if (!winter) for (int i = x + 2; i < x + W - 2; i++) if (((i + r) & 1) || g > 0.3f) cv.put(i, r - 1, g > 0.05f ? crop : SOIL_D);
        }
        // fence
        for (int i = 0; i < W; i += 2) { cv.put(x + i, y, 0xc09473); cv.put(x + i, y + H - 1, 0xc09473); }
        for (int j = 0; j < H; j += 2) { cv.put(x, y + j, 0xc09473); cv.put(x + W - 1, y + j, 0xc09473); }
        // little barn in the corner
        cottage(cv, x + W - 10, y + H - 9, 8, 7, roof, night, winter, 3);
        break;
    }
    case BType::Mill: {
        // tower + turning sails (silhouette)
        int cx = x + W / 2, top = y + 5;
        cv.rect(x + W / 2 - 3, top, 6, H - 6, WALL);
        cv.rect(x + W / 2 + 2, top, 1, H - 6, WALL_D);
        cv.rect(x + W / 2 - 3, top - 1, 6, 2, roof);
        cv.rect(cx - 1, y + H - 3, 2, 3, DOOR);
        cv.rect(x + W / 2 + 3, y + H - 1, 1, 1, SHADOW);
        bool turning = b.staffed > 0 && b.connected && w.store[(int)Res::Wheat] >= 2.f;
        float a0 = turning ? frame * 0.08f : 0.4f;
        for (int k = 0; k < 4; k++) {
            float a = a0 + k * 1.5708f;
            for (int i = 1; i <= 6; i++) cv.put(cx + (int)std::lround(std::cos(a) * i), top + 1 + (int)std::lround(std::sin(a) * i), i > 3 ? 0xffffff : TRUNK);
        }
        break;
    }
    case BType::Bakery: {
        cottage(cv, x + 1, y + 3, W - 2, H - 4, roof, night, winter);
        // chimney with smoke (silhouette)
        cv.rect(x + 3, y, 2, 4, 0x8a4836);
        bool baking = b.staffed > 0 && b.connected && w.store[(int)Res::Flour] >= 1.f;
        if (baking) for (int i = 0; i < 3; i++) cv.put(x + 3 + ((frame / 8 + i) % 2), y - 1 - i - (frame / 6) % 2, 0xc7dcd0);
        cv.put(x + W - 4, y + H - 3, RIPE);   // bread on the sill
        break;
    }
    case BType::Well: {
        cv.rect(x + 2, y + 7, 6, 1, SHADOW);
        cv.rect(x + 1, y + 3, 6, 4, ROCK_L);
        cv.rect(x + 2, y + 4, 4, 2, WATER);
        cv.rect(x + 1, y + 6, 6, 1, ROCK_D);
        cv.put(x + 1, y + 1, TRUNK); cv.put(x + 1, y + 2, TRUNK); cv.put(x + 6, y + 1, TRUNK); cv.put(x + 6, y + 2, TRUNK);
        cv.rect(x, y, 8, 1, roof);
        break;
    }
    case BType::Healer: {
        cottage(cv, x + 1, y + 2, W - 2, H - 3, roof, night, winter);
        // green cross banner
        cv.rect(x + W / 2 - 1, y + 1, 2, 5, 0x1ebc73);
        cv.rect(x + W / 2 - 3, y + 2, 6, 2, 0x1ebc73);
        break;
    }
    case BType::Tower: {
        // tall stone tower (height silhouette) overlapping the tile above
        int tx = x + 4, tw = W - 8, top = y - 6;
        cv.rect(tx + 1, y + H - 1, tw, 1, SHADOW); cv.rect(tx + tw, top + 3, 1, H + 3, SHADOW);
        cv.rect(tx, top + 3, tw, y + H - top - 3, ROCK_L);
        cv.rect(tx + tw - 1, top + 3, 1, y + H - top - 3, ROCK);
        cv.rect(tx, top + 3, 1, y + H - top - 3, 0xc7dcd0);
        for (int i = 0; i < tw; i += 2) cv.rect(tx + i, top, 1, 3, ROCK_L);
        cv.rect(tx, top + 2, tw, 1, roof);
        cv.rect(tx + tw / 2 - 1, top + 6, 2, 3, DOOR);
        cv.rect(tx + tw / 2 - 1, y + H - 3, 2, 3, DOOR);
        if (b.staffed) { cv.put(tx + tw / 2, top - 2, SKIN); cv.put(tx + tw / 2, top - 1, CLOTH); }
        break;
    }
    default: break;
    }
}

// A small speech bubble with a 4x4 icon, bobbing above a building.
void bubble(Canvas& cv, int cx, int y, int frame, int kind) {
    int bob = (frame / 15) % 2;
    int bx = cx - 4, by = y - 9 - bob;
    cv.rect(bx, by, 9, 8, 0x14101a);
    cv.rect(bx + 1, by + 1, 7, 6, 0xffffff);
    cv.put(cx, by + 8, 0x14101a);
    int ix = bx + 2, iy = by + 2;
    switch (kind) {
    case 0:   // bread loaf
        cv.rect(ix, iy + 1, 5, 3, 0xcd683d); cv.rect(ix + 1, iy, 3, 1, 0xe6904e); cv.put(ix + 1, iy + 2, RIPE); cv.put(ix + 3, iy + 2, RIPE); break;
    case 1:   // firewood
        cv.rect(ix, iy + 1, 5, 1, TRUNK); cv.rect(ix, iy + 3, 5, 1, TRUNK); cv.put(ix + 2, iy, FIRE1); cv.put(ix + 2, iy + 2, FIRE2); break;
    case 2:   // road sign (not connected)
        cv.rect(ix, iy, 5, 3, 0xe83b3b); cv.rect(ix + 1, iy + 1, 3, 1, 0xffffff); cv.put(ix + 2, iy + 3, TRUNK); break;
    case 3:   // wheat (mill starved)
        for (int i = 0; i < 3; i++) cv.rect(ix + 1 + i, iy + 1 - (i == 1), 1, 3, RIPE); cv.rect(ix + 1, iy + 3, 3, 1, 0xcd683d); break;
    case 4:   // flour sack
        cv.rect(ix + 1, iy + 1, 3, 3, 0xe8e0d0); cv.put(ix + 2, iy, 0xab947a); break;
    case 5:   // log (sawmill starved)
        cv.rect(ix, iy + 1, 5, 2, TRUNK); cv.put(ix, iy + 1, 0xc09473); break;
    case 6:   // no workers: person
        cv.put(ix + 2, iy, SKIN); cv.rect(ix + 1, iy + 1, 3, 2, CLOTH); cv.put(ix + 1, iy + 3, DOOR); cv.put(ix + 3, iy + 3, DOOR); break;
    }
}

void drawPerson(Canvas& cv, float x, float y, uint32_t body, uint32_t head, int frame, bool moving, int carry) {
    int px = (int)std::lround(x * TILE_PX) + 3, py = (int)std::lround(y * TILE_PX) + 2;
    int step = moving && (frame / 5) % 2;
    cv.put(px + 1, py + 6, SHADOW); cv.put(px + 2, py + 6, SHADOW);
    cv.put(px, py, head); cv.put(px + 1, py, head);
    cv.rect(px, py + 1, 2, 2, body);
    cv.put(px + 1, py + 2, darken(body));
    cv.put(px + (step ? 0 : 0), py + 3 + 0, 0x2e222f); cv.put(px + 1, py + 3 + step, 0x2e222f);
    if (step) cv.put(px, py + 4, 0x2e222f); else cv.put(px + 1, py + 4, 0x2e222f);
    if (carry) cv.put(px + 2, py, (uint32_t)carry);
}

void circle(Canvas& cv, float cx, float cy, float r, uint32_t col, int frame) {
    int steps = std::max(24, (int)(r * 6));
    for (int i = 0; i < steps; i++) {
        if ((i + frame / 3) % 4 == 0) continue;   // marching ants
        float a = i * 6.2831853f / steps;
        cv.put((int)std::lround(cx + std::cos(a) * r), (int)std::lround(cy + std::sin(a) * r), col);
    }
}

void outline(Canvas& cv, int x, int y, int w, int h, uint32_t c) {
    for (int i = 0; i < w; i++) { cv.put(x + i, y, c); cv.put(x + i, y + h - 1, c); }
    for (int j = 0; j < h; j++) { cv.put(x, y + j, c); cv.put(x + w - 1, y + j, c); }
}

// Idle reason for the bubble, or -1 when the building is working.
int idleReason(const World& w, const Building& b) {
    if (b.type == BType::Hall || b.type == BType::Well) return -1;
    if (!b.connected) return 2;
    if (b.type == BType::House) return b.hungry ? 0 : (b.cold ? 1 : -1);
    if (binfo(b.type).workers > 0 && b.staffed == 0) return 6;
    if (b.type == BType::Mill && w.store[(int)Res::Wheat] < 2.f) return 3;
    if (b.type == BType::Bakery && w.store[(int)Res::Flour] < 1.f) return 4;
    if (b.type == BType::Sawmill && w.store[(int)Res::Logs] < 9.f) return 5;
    return -1;
}
} // namespace

void drawWorld(const World& w, uint32_t* px, int pitchPx, int frame, const Overlay& ov) {
    Canvas cv{px, pitchPx};
    bool winter = w.season() == Season::Winter;
    float t = w.dayFrac();
    bool lamps = t > 0.72f || t < 0.12f;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) drawTile(cv, w, tx, ty, frame, winter);

    // buildings top to bottom so tall ones overlap correctly
    std::vector<int> order;
    for (int i = 0; i < (int)w.buildings.size(); i++) if (w.buildings[i].alive) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return w.buildings[a].y < w.buildings[b].y; });
    for (int i : order) {
        const Building& b = w.buildings[i];
        int why = idleReason(w, b);
        cv.desat = (why >= 2 && b.type != BType::House) || why == 2 ? 0.5f : 0.f;   // stuck buildings go grey
        drawBuilding(cv, w, b, frame, lamps, winter);
        cv.desat = 0.f;
    }
    // fire on top of buildings
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (w.at(tx, ty).fire > 0.f) drawFire(cv, tx * TILE_PX, ty * TILE_PX, hashc(tx, ty), frame);

    for (const Villager& v : w.villagers) {
        if (!v.alive) continue;
        bool moving = v.pathPos < v.path.size();
        int carry = 0;
        if (v.carrying && v.work >= 0) carry = w.buildings[v.work].type == BType::Lumber ? 0x8a5a32 : 0x8fd3ff;
        bool kid = v.age < 6.f;
        uint32_t body = v.sick ? SICK : (v.work >= 0 ? roofOf(w.buildings[v.work].type) : (kid ? 0xf5a097 : CLOTH));
        drawPerson(cv, v.x, v.y, body, v.sick ? SICK : SKIN, frame, moving, carry);
    }
    for (const Raider& r : w.raiders)
        if (r.alive) drawPerson(cv, r.x, r.y, RAIDER, RAIDER_D, frame, true, r.loot > 0 ? 0xf9c22b : 0);
    for (const Arrow& a : w.arrows) {
        int age = w.ticks - a.born;
        if (age < 0 || age > 3) continue;
        float k0 = age / 4.f, k1 = (age + 1) / 4.f;
        cv.line(a.x0 * TILE_PX + (a.x1 - a.x0) * TILE_PX * k0, a.y0 * TILE_PX - 6 + (a.y1 - a.y0) * TILE_PX * k0,
                a.x0 * TILE_PX + (a.x1 - a.x0) * TILE_PX * k1, a.y0 * TILE_PX - 6 + (a.y1 - a.y0) * TILE_PX * k1, 0xffffff);
    }

    // day/night + drought tint
    float night = 0.f;
    if (t > 0.70f) night = std::min(1.f, (t - 0.70f) / 0.12f);
    else if (t < 0.12f) night = 1.f - t / 0.12f;
    night *= 0.5f;
    float dry = w.droughtDays > 0 ? 0.12f : 0.f;
    if (night > 0.f || dry > 0.f)
        for (int y = 0; y < VIEW_H; y++)
            for (int x = 0; x < VIEW_W; x++) {
                uint32_t& p = px[y * pitchPx + x];
                if (p == LIT || p == FIRE1 || p == FIRE2 || p == FIRE3) continue;   // lights glow through
                uint32_t c = p;
                if (dry > 0.f) c = mixc(c, 0xfbb954, dry);
                if (night > 0.f) c = mixc(c, 0x1a1b33, night);
                p = c;
            }

    // need / stuck icons (after the tint so they stay readable)
    for (int i : order) {
        const Building& b = w.buildings[i];
        int why = idleReason(w, b);
        if (why < 0) continue;
        const BInfo& in = binfo(b.type);
        bubble(cv, b.x * TILE_PX + in.w * TILE_PX / 2, b.y * TILE_PX + (b.type == BType::Tower ? -6 : 0), frame + i * 7, why);
    }

    if (ov.selected >= 0 && ov.selected < (int)w.buildings.size() && w.buildings[ov.selected].alive) {
        const Building& b = w.buildings[ov.selected];
        const BInfo& in = binfo(b.type);
        outline(cv, b.x * TILE_PX - 1, b.y * TILE_PX - 1, in.w * TILE_PX + 2, in.h * TILE_PX + 2, 0xffffff);
        if (in.radius > 0.f) circle(cv, (b.x + in.w / 2.f) * TILE_PX, (b.y + in.h / 2.f) * TILE_PX, in.radius * TILE_PX, 0xffffff, frame);
    }

    // road preview
    for (int i : ov.roadTiles) {
        int x = i % MAP_W, y = i / MAP_W;
        bool ok = w.canRoad(x, y) || w.at(x, y).t == Tile::Road;
        for (int j = 0; j < 8; j++) for (int k = 0; k < 8; k++) cv.blend(x * TILE_PX + k, y * TILE_PX + j, ok ? 0x9cdb43 : 0xe83b3b, 0.45f);
    }

    // demolish target
    if (ov.demolishX >= 0 && w.inside(ov.demolishX, ov.demolishY)) {
        int bi = w.buildingAt(ov.demolishX, ov.demolishY);
        int x = ov.demolishX, y = ov.demolishY, bw = 1, bh = 1;
        if (bi >= 0) { x = w.buildings[bi].x; y = w.buildings[bi].y; bw = binfo(w.buildings[bi].type).w; bh = binfo(w.buildings[bi].type).h; }
        for (int j = 0; j < bh * 8; j++) for (int k = 0; k < bw * 8; k++) cv.blend(x * 8 + k, y * 8 + j, 0xe83b3b, 0.4f);
        outline(cv, x * 8, y * 8, bw * 8, bh * 8, 0xe83b3b);
    }

    // placement ghost: tinted footprint, see-through sprite, coverage circle lighting up houses inside
    if (ov.ghost >= 0) {
        BType t = (BType)ov.ghost;
        const BInfo& in = binfo(t);
        uint32_t tint = ov.ghostOk ? 0x9cdb43 : 0xe83b3b;
        float cx = (ov.gx + in.w / 2.f) * TILE_PX, cy = (ov.gy + in.h / 2.f) * TILE_PX;
        if (in.radius > 0.f) {
            float r = in.radius;
            for (const Building& b : w.buildings) {
                if (!b.alive) continue;
                const BInfo& bi = binfo(b.type);
                bool serve = (t == BType::Well || t == BType::Healer || t == BType::Tower) ? (b.type == BType::House || b.type == BType::Hall) : false;
                if (!serve) continue;
                float bx = b.x + bi.w / 2.f, by = b.y + bi.h / 2.f;
                if (std::hypot(bx - (ov.gx + in.w / 2.f), by - (ov.gy + in.h / 2.f)) <= r)
                    outline(cv, b.x * 8 - 1, b.y * 8 - 1, bi.w * 8 + 2, bi.h * 8 + 2, 0xf9c22b);
            }
            // work area: show trees / water the building will use
            if (t == BType::Lumber || t == BType::Fisher) {
                Tile want = t == BType::Lumber ? Tile::Forest : Tile::Water;
                for (int y = (int)(ov.gy + in.h / 2.f - r); y <= ov.gy + in.h / 2.f + r; y++)
                    for (int x = (int)(ov.gx + in.w / 2.f - r); x <= ov.gx + in.w / 2.f + r; x++)
                        if (w.inside(x, y) && w.at(x, y).t == want && std::hypot(x + 0.5f - (ov.gx + in.w / 2.f), y + 0.5f - (ov.gy + in.h / 2.f)) <= r)
                            for (int j = 2; j < 6; j++) for (int k = 2; k < 6; k++) cv.blend(x * 8 + k, y * 8 + j, 0xffffff, 0.35f);
            }
            circle(cv, cx, cy, r * TILE_PX, ov.ghostOk ? 0xffffff : 0xe83b3b, frame);
        }
        // sprite drawn into a scratch area would be nicer; tinting the footprint keeps it simple
        Building fake; fake.type = t; fake.x = ov.gx; fake.y = ov.gy; fake.connected = true; fake.staffed = 1; fake.grow = 0.6f;
        drawBuilding(cv, w, fake, frame, false, winter);
        for (int j = 0; j < in.h * 8; j++)
            for (int k = 0; k < in.w * 8; k++) cv.blend(ov.gx * 8 + k, ov.gy * 8 + j, tint, 0.35f);
        outline(cv, ov.gx * 8, ov.gy * 8, in.w * 8, in.h * 8, tint);
    }
}
