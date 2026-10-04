// 16x16 pixel-art renderer. Everything is drawn procedurally in code (no image assets):
// textured terrain, y-sorted trees and buildings with outlines and top-left light,
// sprite-grid villagers with a walk cycle, need bubbles, and the placement overlays.
#include "render.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace sim;

namespace {
constexpr int T = TILE_PX;
// Resurrect 64 palette (Lospec)
constexpr uint32_t OUTLINE = 0x2e222f;
constexpr uint32_t DEEP = 0x323353, DEEP_HI = 0x484a77, WATER = 0x4d65b4, WATER_HI = 0x4d9be6, SHALLOW = 0x4d9be6, FOAM = 0x8fd3ff;
constexpr uint32_t SAND = 0xe6904e, SAND_L = 0xfbb954, SAND_D = 0xcd683d;
constexpr uint32_t GRASS_DD = 0x165a4c, GRASS_D = 0x1e7f55, GRASS = 0x239063, GRASS_L = 0x1ebc73, GRASS_LL = 0x91db69;
constexpr uint32_t LEAF_D = 0x165a4c, LEAF = 0x239063, LEAF_L = 0x1ebc73, LEAF_LL = 0x91db69, PINE_D = 0x0b3d33, PINE = 0x165a4c, PINE_L = 0x239063;
constexpr uint32_t TRUNK = 0x4c3e24, TRUNK_L = 0x7a4a2a, WOOD = 0xa77b5b, WOOD_L = 0xc09473, WOOD_D = 0x80553e;
constexpr uint32_t ROCK = 0x7f708a, ROCK_L = 0x9babb2, ROCK_LL = 0xc7dcd0, ROCK_D = 0x625565, ROCK_DD = 0x3e3546, SNOW = 0xe8f0ee;
constexpr uint32_t SOIL = 0x694f62, SOIL_D = 0x45293f, SPROUT = 0x91db69, RIPE = 0xf9c22b, RIPE_D = 0xe0a83a;
constexpr uint32_t ROAD = 0xa77b5b, ROAD_D = 0x80553e, ROAD_L = 0xc09473;
constexpr uint32_t WALL = 0xe8d6b5, WALL_D = 0xc4ae8c, BEAM = 0x6e4a3a, DOOR = 0x5a3b2a, DOOR_D = 0x3b2618, LIT = 0xfbff86, GLASS = 0x4d65b4, GLASS_L = 0x8fd3ff;
constexpr uint32_t ASH = 0x313638, ASH_L = 0x45444f;
constexpr uint32_t FIRE1 = 0xfb6b1d, FIRE2 = 0xf9c22b, FIRE3 = 0xea4f36, SMOKE = 0xc7dcd0;
constexpr uint32_t SKIN = 0xfdcbb0, SKIN_D = 0xe69c69, SICK = 0xa2a947, PANTS = 0x484a77, SHOE = 0x2e222f, RAIDER = 0xb33831, HELM = 0x9babb2;
constexpr uint32_t ROOF_HOME = 0xb33831, ROOF_FOOD = 0xe0a83a, ROOF_WOOD = 0x5b6b2e, ROOF_SAFE = 0x5a6e9c, ROOF_HALL = 0x6b3e75;
const uint32_t HAIR[5] = {0x4c3e24, 0xf9c22b, 0x2e222f, 0xcd683d, 0x7a4a2a};

uint32_t hashc(int x, int y, int z = 0) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
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
uint32_t grey(uint32_t c, float t) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, l = (r * 3 + g * 6 + b) / 10;
    return mixc(c, (uint32_t)(l << 16 | l << 8 | l), t);
}
uint32_t lighten(uint32_t c, float t = 0.3f) { return mixc(c, 0xffffff, t); }
uint32_t darken(uint32_t c, float t = 0.35f) { return mixc(c, 0x000000, t); }
uint32_t snowy(uint32_t c, bool winter, float t = 0.7f) { return winter ? mixc(c, SNOW, t) : c; }

struct Canvas {
    uint32_t* px; int pitch;
    int cx0 = 0, cy0 = 0, cx1 = VIEW_W, cy1 = VIEW_H;   // clip (visible region)
    float desat = 0.f;
    bool in(int x, int y) const { return x >= cx0 && y >= cy0 && x < cx1 && y < cy1; }
    void put(int x, int y, uint32_t c) { if (in(x, y)) px[y * pitch + x] = desat > 0.f ? grey(c, desat) : c; }
    void rect(int x, int y, int w, int h, uint32_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, c); }
    void blend(int x, int y, uint32_t c, float t) { if (in(x, y)) { uint32_t& p = px[y * pitch + x]; p = mixc(p, c, t); } }
    void blendRect(int x, int y, int w, int h, uint32_t c, float t) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) blend(x + i, y + j, c, t); }
    void line(float x0, float y0, float x1, float y1, uint32_t c) {
        int n = (int)std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
        for (int i = 0; i <= n; i++) { float t = (float)i / n; put((int)std::lround(x0 + (x1 - x0) * t), (int)std::lround(y0 + (y1 - y0) * t), c); }
    }
    void outlineRect(int x, int y, int w, int h, uint32_t c) {
        for (int i = 0; i < w; i++) { put(x + i, y, c); put(x + i, y + h - 1, c); }
        for (int j = 0; j < h; j++) { put(x, y + j, c); put(x + w - 1, y + j, c); }
    }
    // soft ground shadow ellipse
    void shadow(int cx, int cy, int rx, int ry, float t = 0.35f) {
        for (int y = -ry; y <= ry; y++) for (int x = -rx; x <= rx; x++)
            if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) blend(cx + x, cy + y, 0x0b1a1a, t);
    }
};

// ---------------------------------------------------------------- terrain
bool isWaterT(Tile t) { return t == Tile::Water || t == Tile::Deep; }
bool roadAt(const World& w, int x, int y) { return w.inside(x, y) && w.at(x, y).t == Tile::Road; }

void grassTile(Canvas& cv, int ox, int oy, int tx, int ty, bool winter) {
    uint32_t th = hashc(tx, ty, 7);
    uint32_t base = snowy(GRASS, winter, 0.8f);
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            uint32_t h = hashc(ox + x, oy + y);
            uint32_t c = base;
            int v = h & 63;
            if (v < 7) c = snowy(GRASS_D, winter, 0.75f);
            else if (v < 10) c = snowy(GRASS_L, winter, 0.85f);
            cv.put(ox + x, oy + y, c);
        }
    // grass tufts: little "v" blades, lit on top
    for (int k = 0; k < 3; k++) {
        uint32_t h = th >> (k * 8);
        int x = ox + 2 + (h & 11), y = oy + 3 + ((h >> 4) & 9);
        uint32_t d = snowy(GRASS_D, winter, 0.6f), l = snowy(GRASS_L, winter, 0.9f);
        cv.put(x, y + 1, d); cv.put(x + 2, y + 1, d); cv.put(x + 1, y + 2, d);
        cv.put(x, y, l); cv.put(x + 2, y, l);
    }
    if (!winter && (th % 11) == 0) {   // a few flowers
        int x = ox + 3 + ((th >> 9) & 7), y = oy + 3 + ((th >> 13) & 7);
        uint32_t f = (th >> 17) & 1 ? 0xfdf7ed : 0xf9c22b;
        cv.put(x, y, f); cv.put(x + 1, y + 1, GRASS_LL);
    }
}

void waterTile(Canvas& cv, const World& w, int ox, int oy, int tx, int ty, int frame, bool deep) {
    bool n = w.inside(tx, ty - 1) && !isWaterT(w.at(tx, ty - 1).t), s = w.inside(tx, ty + 1) && !isWaterT(w.at(tx, ty + 1).t);
    bool e = w.inside(tx + 1, ty) && !isWaterT(w.at(tx + 1, ty).t), ww = w.inside(tx - 1, ty) && !isWaterT(w.at(tx - 1, ty).t);
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            int edge = 99;
            if (n) edge = std::min(edge, y);
            if (s) edge = std::min(edge, T - 1 - y);
            if (ww) edge = std::min(edge, x);
            if (e) edge = std::min(edge, T - 1 - x);
            uint32_t c = deep ? DEEP : WATER;
            if (edge <= 3) c = deep ? WATER : SHALLOW;
            if (edge == 0 && ((x + y + frame / 10) % 4)) c = FOAM;
            cv.put(ox + x, oy + y, c);
        }
    // drifting ripples
    uint32_t h = hashc(tx, ty);
    int phase = (frame / 12 + (int)(h & 15)) % 16;
    if (phase < 10) {
        int rx = ox + 2 + ((h >> 4) & 7), ry = oy + 3 + ((h >> 8) & 9);
        uint32_t rc = deep ? DEEP_HI : WATER_HI;
        int len = phase < 5 ? phase : 10 - phase;
        for (int i = 0; i < len; i++) cv.put(rx + i, ry, rc);
    }
}

void sandTile(Canvas& cv, int ox, int oy, bool winter) {
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            uint32_t h = hashc(ox + x, oy + y, 3) & 31;
            uint32_t c = h < 3 ? SAND_D : (h < 5 ? SAND_L : SAND);
            cv.put(ox + x, oy + y, snowy(c, winter, 0.45f));
        }
}

void rockTile(Canvas& cv, const Cell& c, int ox, int oy, int tx, int ty, bool winter) {
    uint32_t h = hashc(tx, ty, 5);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) cv.put(ox + x, oy + y, (hashc(ox + x, oy + y, 9) & 15) == 0 ? ROCK_D : ROCK);
    // a boulder lump: lit top-left faces, dark bottom-right
    int cx = ox + 7 + (h & 1), cy = oy + 8;
    for (int y = -7; y <= 7; y++)
        for (int x = -7; x <= 7; x++) {
            float d = std::sqrt((float)(x * x) + y * y * 1.3f);
            if (d > 7.f) continue;
            uint32_t col = ROCK;
            if (x + y < -4) col = ROCK_L;
            if (x + y < -8) col = ROCK_LL;
            if (x + y > 5) col = ROCK_D;
            if (d > 6.2f) col = ROCK_DD;
            cv.put(cx + x, cy + y, col);
        }
    cv.line(cx - 2, cy - 1, cx + 1, cy + 2, ROCK_D);   // crack
    if (c.height > 0.7f || winter)
        for (int y = -7; y <= -3; y++) for (int x = -5; x <= 5; x++)
            if (std::sqrt((float)(x * x) + y * y * 1.3f) < 6.5f && (y < -4 || ((x + (int)h) & 1))) cv.put(cx + x, cy + y, SNOW);
}

void roadTile(Canvas& cv, const World& w, int ox, int oy, int tx, int ty, bool winter) {
    grassTile(cv, ox, oy, tx, ty, winter);
    bool n = roadAt(w, tx, ty - 1), s = roadAt(w, tx, ty + 1), e = roadAt(w, tx + 1, ty), ww = roadAt(w, tx - 1, ty);
    // also connect into buildings so doors meet the road
    auto bld = [&](int x, int y) { return w.inside(x, y) && w.at(x, y).t == Tile::Building; };
    n = n || bld(tx, ty - 1); s = s || bld(tx, ty + 1); e = e || bld(tx + 1, ty); ww = ww || bld(tx - 1, ty);
    auto on = [&](int x, int y) {
        bool core = x >= 3 && x <= 12 && y >= 3 && y <= 12;
        bool arm = (n && y < 3 && x >= 3 && x <= 12) || (s && y > 12 && x >= 3 && x <= 12) ||
                   (ww && x < 3 && y >= 3 && y <= 12) || (e && x > 12 && y >= 3 && y <= 12);
        return core || arm;
    };
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            if (!on(x, y)) continue;
            uint32_t h = hashc(ox + x, oy + y, 11) & 31;
            uint32_t c = h < 3 ? ROAD_D : (h < 6 ? ROAD_L : ROAD);
            // darker rim where the dirt meets grass (bottom/right shade)
            bool rimB = !on(x, y + 1) && y + 1 < T, rimR = !on(x + 1, y) && x + 1 < T;
            bool rimT = y > 0 && !on(x, y - 1), rimL = x > 0 && !on(x - 1, y);
            if (rimB || rimR) c = ROAD_D;
            else if (rimT || rimL) c = ROAD_L;
            cv.put(ox + x, oy + y, snowy(c, winter, 0.35f));
        }
}

void ashTile(Canvas& cv, const Cell& c, int ox, int oy, int tx, int ty, int frame) {
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) cv.put(ox + x, oy + y, (hashc(ox + x, oy + y, 13) & 7) == 0 ? ASH_L : ASH);
    uint32_t h = hashc(tx, ty);
    if (c.res > 2.f && ((frame / 8 + h) % 5) == 0) cv.put(ox + (h & 15), oy + ((h >> 4) & 15), FIRE1);   // embers
    if (c.res < 1.5f) for (int k = 0; k < 4; k++) cv.put(ox + ((h >> (k * 5)) & 15), oy + ((h >> (k * 5 + 2)) & 15), k & 1 ? GRASS : GRASS_D);
}

void drawFire(Canvas& cv, int ox, int oy, uint32_t h, int frame) {
    for (int i = 0; i < 6; i++) {
        int fx = ox + 2 + (int)((h >> (i * 4)) % 12);
        int life = (frame / 2 + (int)(h >> i) + i * 5) % 12;
        int fy = oy + 15 - life;
        int sz = std::max(0, 3 - life / 4);
        uint32_t c = life < 4 ? FIRE2 : (life < 8 ? FIRE1 : FIRE3);
        for (int y = 0; y <= sz; y++) for (int x = -sz + y; x <= sz - y; x++) cv.put(fx + x, fy - y, c);
        if (life > 9) cv.blend(fx, fy - 3, SMOKE, 0.5f);
    }
}

// ---------------------------------------------------------------- trees
void drawTree(Canvas& cv, const Cell& c, int tx, int ty, bool winter, int frame) {
    int ox = tx * T, oy = ty * T;
    uint32_t h = hashc(tx, ty, 21);
    bool big = c.res > 1.5f;
    bool pine = (h & 3) == 0;
    int jx = (int)((h >> 3) % 5) - 2;
    int baseX = ox + 8 + jx, baseY = oy + 14;
    cv.shadow(baseX + 2, baseY, big ? 6 : 4, 2);
    if (pine) {
        int H = big ? 18 : 12;
        cv.rect(baseX - 1, baseY - 3, 2, 4, TRUNK);
        for (int r = 0; r < H; r++) {
            int y = baseY - 3 - r;
            int tier = r % (H / 3);
            int half = 1 + (H / 3 - tier) * (big ? 6 : 4) / (H / 3) - (r * 2 / H);
            half = std::max(0, half);
            for (int x = -half; x <= half; x++) {
                uint32_t col = x < -half / 2 ? PINE_L : (x > half / 2 ? PINE_D : PINE);
                if (x == -half || x == half) col = OUTLINE;
                if (winter && tier < 2 && std::abs(x) < half) col = SNOW;
                cv.put(baseX + x, y, col);
            }
        }
        cv.put(baseX, baseY - 3 - H, OUTLINE);
    } else {
        float r = big ? 6.5f : 4.5f;
        int cy = baseY - (big ? 9 : 7);
        cv.rect(baseX - 1, cy + 2, 2, baseY - cy - 1, TRUNK);
        cv.put(baseX - 1, cy + 3, TRUNK_L);
        float sway = std::sin(frame * 0.05f + (h & 255)) * 0.6f;
        for (int y = -8; y <= 8; y++)
            for (int x = -8; x <= 8; x++) {
                // lumpy canopy: radius varies with angle
                float a = std::atan2((float)y, (float)x);
                float rr = r + 0.8f * std::sin(a * 5.f + (h & 7)) + (y < 0 ? sway * 0.3f : 0.f);
                float d = std::sqrt((float)(x * x + y * y));
                if (d > rr) continue;
                uint32_t col = LEAF;
                float lit = -(x + y) / rr;
                if (lit > 0.45f) col = LEAF_L;
                if (lit > 0.95f) col = LEAF_LL;
                if (lit < -0.5f) col = LEAF_D;
                if (d > rr - 1.f) col = OUTLINE;
                if (winter && col != OUTLINE) col = y < -1 ? SNOW : mixc(col, SNOW, 0.3f);
                // leaf clumps
                if (col == LEAF && (hashc(x + 9, y + 9, h) & 7) == 0) col = LEAF_L;
                cv.put(baseX + x, cy + y, col);
            }
        if (!winter && big && (h & 16)) { cv.put(baseX - 3, cy + 1, 0xea4f36); cv.put(baseX + 2, cy - 2, 0xea4f36); }   // apples
    }
}

// ---------------------------------------------------------------- buildings
uint32_t roofOf(BType t) {
    if (t == BType::Hall) return ROOF_HALL;
    switch (binfo(t).cat) {
    case Category::Home: return ROOF_HOME;
    case Category::Food: return ROOF_FOOD;
    case Category::Wood: return ROOF_WOOD;
    default: return ROOF_SAFE;
    }
}

void window(Canvas& cv, int x, int y, bool night) {
    cv.rect(x - 1, y - 1, 6, 6, BEAM);
    cv.rect(x, y, 4, 4, night ? LIT : GLASS);
    if (!night) { cv.put(x, y, GLASS_L); cv.put(x + 1, y, GLASS_L); cv.put(x, y + 1, GLASS_L); }
    cv.rect(x + 2, y, 0, 4, BEAM);
    for (int j = 0; j < 4; j++) cv.put(x + 2, y + j, BEAM);
    for (int i = 0; i < 4; i++) cv.put(x + i, y + 2, BEAM);
    cv.rect(x - 1, y + 5, 6, 1, WOOD_L);   // sill
}

void door(Canvas& cv, int x, int y, int h) {
    cv.rect(x - 1, y - 1, 7, h + 1, BEAM);
    cv.rect(x, y, 5, h, DOOR);
    for (int j = 0; j < h; j++) cv.put(x + 2, y + j, DOOR_D);
    cv.put(x + 3, y + h / 2, RIPE);
    cv.rect(x - 1, y + h, 7, 1, ROCK_L);   // step
}

// Timber cottage in the box (x,y,w,h): shingled roof of height roofH over a framed wall.
void cottage(Canvas& cv, int x, int y, int w, int h, uint32_t roof, bool night, bool winter, int roofH, uint32_t wall = WALL) {
    int wy = y + roofH;
    cv.shadow(x + w / 2 + 3, y + h, w / 2 + 1, 3, 0.3f);
    cv.blendRect(x + w, wy + 2, 3, h - roofH - 1, 0x0b1a1a, 0.3f);
    // walls
    cv.rect(x + 1, wy, w - 2, h - roofH, wall);
    cv.rect(x + w - 4, wy, 3, h - roofH, darken(wall, 0.15f));
    for (int j = wy; j < y + h; j++) { cv.put(x + 1, j, BEAM); cv.put(x + w - 2, j, BEAM); }
    for (int i = x + 1; i < x + w - 1; i++) { cv.put(i, wy, BEAM); cv.put(i, y + h - 1, darken(wall, 0.3f)); }
    cv.line(x + 1, y + h - 1, x + 1, wy, OUTLINE); cv.line(x + w - 1, y + h - 1, x + w - 1, wy, OUTLINE);
    cv.line(x + 1, y + h, x + w - 1, y + h, OUTLINE);
    // roof: a trapezoid that overhangs the wall by 1px, rows of shingles
    for (int r = 0; r < roofH; r++) {
        int inset = (roofH - 1 - r) * (w / 3) / std::max(1, roofH - 1);
        int x0 = x + inset - (r == roofH - 1 ? 1 : 0), x1 = x + w - 1 - inset + (r == roofH - 1 ? 1 : 0);
        for (int i = x0; i <= x1; i++) {
            uint32_t c = roof;
            bool shingleLine = (roofH - 1 - r) % 3 == 0;
            bool tick = ((i + (r / 3) * 2) % 4) == 0 && !shingleLine;
            if (shingleLine) c = darken(roof, 0.2f);
            else if (tick) c = darken(roof, 0.12f);
            if (i - x0 < 2) c = lighten(roof, 0.25f);
            if (x1 - i < 2) c = darken(roof, 0.3f);
            if (r == 0) c = lighten(roof, 0.35f);
            if (winter && r < roofH * 2 / 3 && i > x0 && i < x1) c = ((i + r) % 5) ? SNOW : ROCK_LL;
            if (i == x0 || i == x1) c = OUTLINE;
            cv.put(i, y + r, c);
        }
        if (r == 0) for (int i = x0; i <= x1; i++) cv.put(i, y - 1, OUTLINE);
    }
    (void)night;
}

void smoke(Canvas& cv, int x, int y, int frame, int seed) {
    for (int i = 0; i < 4; i++) {
        int life = (frame / 3 + seed + i * 7) % 28;
        int px = x + (int)std::lround(std::sin((life + seed) * 0.3f) * 1.5f) + life / 8;
        int py = y - life / 2;
        int sz = life / 10;
        float a = 0.75f - life / 40.f;
        for (int yy = -sz; yy <= sz; yy++) for (int xx = -sz; xx <= sz; xx++) cv.blend(px + xx, py + yy, SMOKE, a);
    }
}

// ---------------------------------------------------------------- stock piles
// Goods are not counted in the HUD; they sit as piles on the map instead.
// How full a pile is: the town's stock split over the buildings that hold it.
int pileLevel(const World& w, Res r, int holders, float step, int maxN) {
    if (holders <= 0) return 0;
    float per = w.store[(int)r] / holders;
    if (per <= 0.05f) return 0;
    return std::min(maxN, (int)std::ceil(per / step));
}
// logs stacked in a pyramid, end grain facing us; (x, bottom) = lower-left
void logPile(Canvas& cv, int x, int bottom, int n) {
    int base = n >= 6 ? 3 : (n >= 3 ? 2 : 1), k = 0;
    for (int r = 0; k < n && r < 3; r++)
        for (int i = 0; i < base - r && k < n; i++, k++) {
            int lx = x + i * 4 + r * 2, ly = bottom - 4 - r * 3;
            cv.rect(lx, ly, 4, 4, OUTLINE); cv.rect(lx + 1, ly + 1, 2, 2, WOOD_L); cv.put(lx + 1, ly + 1, RIPE_D);
        }
}
// planks: n boards, 2px each, w wide
void plankPile(Canvas& cv, int x, int bottom, int n, int w = 11) {
    if (n <= 0) return;
    cv.rect(x - 1, bottom - 2 * n - 1, w + 3, 2 * n + 2, OUTLINE);
    for (int i = 0; i < n; i++) {
        int yy = bottom - 2 - i * 2, off = (i & 1);
        cv.rect(x + off, yy, w, 1, i & 1 ? WOOD : WOOD_L);
        cv.rect(x + off, yy + 1, w, 1, WOOD_D);
        cv.put(x + off + (i * 5 + 3) % w, yy, WOOD_D);   // knot
    }
}
// sacks (flour) or sheaves (wheat), up to 2 rows
void sacks(Canvas& cv, int x, int bottom, int n, bool wheat) {
    for (int k = 0; k < n; k++) {
        int r = k / 3, i = k % 3;
        int sx = x + i * 5 + r * 2, sy = bottom - 6 - r * 4;
        if (wheat) {
            cv.rect(sx + 1, sy, 3, 6, RIPE_D); cv.rect(sx, sy, 5, 2, RIPE); cv.put(sx + 2, sy + 3, WOOD_D);
            cv.put(sx - 1, sy + 5, OUTLINE); cv.put(sx + 5, sy + 5, OUTLINE);
        } else {
            cv.rect(sx, sy + 1, 5, 5, OUTLINE); cv.rect(sx + 1, sy + 1, 3, 4, 0xfdf7ed); cv.put(sx + 3, sy + 4, WALL_D);
            cv.rect(sx + 2, sy, 1, 1, OUTLINE);
        }
    }
}
// open crate with loaves or fish on top
void crate(Canvas& cv, int x, int bottom, int n, bool fish) {
    cv.rect(x, bottom - 5, 9, 5, OUTLINE); cv.rect(x + 1, bottom - 4, 7, 3, WOOD); cv.rect(x + 1, bottom - 3, 7, 1, WOOD_D);
    for (int k = 0; k < n; k++) {
        int i = k % 3, r = k / 3, fx = x + 1 + i * 3 - r, fy = bottom - 7 - r * 2;
        if (fish) { cv.rect(fx, fy, 3, 2, 0x8fd3ff); cv.put(fx + 2, fy + 1, 0x4d9be6); }
        else { cv.rect(fx, fy, 3, 2, 0xcd683d); cv.put(fx + 1, fy, SAND_L); }
    }
}

void drawBuilding(Canvas& cv, const World& w, const Building& b, int frame, bool night, bool winter, int seed) {
    const BInfo& in = binfo(b.type);
    int x = b.x * T, y = b.y * T, W = in.w * T, H = in.h * T;
    uint32_t roof = roofOf(b.type);
    switch (b.type) {
    case BType::Hall: {
        cottage(cv, x + 2, y + 12, W - 5, H - 14, roof, night, winter, 16, 0xf0e2c8);
        // central bell tower
        int tx = x + W / 2 - 5;
        cv.rect(tx, y + 2, 10, 14, ROCK_L); cv.rect(tx + 7, y + 2, 3, 14, ROCK);
        cv.outlineRect(tx - 1, y + 1, 12, 16, OUTLINE);
        cv.rect(tx + 3, y + 5, 4, 5, OUTLINE); cv.rect(tx + 4, y + 7, 2, 2, RIPE);   // bell
        for (int i = 0; i < 12; i += 3) cv.rect(tx - 1 + i, y - 1, 2, 2, ROCK_L);
        cv.line(tx + 5, y - 1, tx + 5, y - 9, OUTLINE);
        int wave = (frame / 8) % 2;
        cv.rect(tx + 6, y - 9, 6, 3, 0xf9c22b); cv.rect(tx + 6 + 2 * wave, y - 7, 4, 1, 0xfbb954);
        window(cv, x + 7, y + 32, night); window(cv, x + W - 13, y + 32, night);
        door(cv, x + W / 2 - 3, y + H - 10, 8);
        // the town's store: start stock and anything no producer is holding
        logPile(cv, x + 2, y + H, pileLevel(w, Res::Logs, 1 + w.count(BType::Lumber), 3.f, 6));
        plankPile(cv, x + W - 15, y + H - 1, pileLevel(w, Res::Planks, 1 + w.count(BType::Sawmill), 4.f, 4), 10);
        if (w.food() > 0.05f) crate(cv, x + 12, y + H - 1, std::min(5, (int)std::ceil(w.food() / std::max(1, 1 + w.count(BType::Bakery) + w.count(BType::Fisher)) / 8.f)), w.store[(int)Res::Fish] > w.store[(int)Res::Bread]);
        break;
    }
    case BType::House: {
        cottage(cv, x + 2, y + 4, W - 4, H - 6, roof, night, winter, 13);
        cv.rect(x + W - 11, y + 1, 4, 7, 0x8a4836); cv.outlineRect(x + W - 12, y, 6, 8, OUTLINE);
        if (!b.cold) smoke(cv, x + W - 9, y - 2, frame, seed);
        window(cv, x + 6, y + 19, night);
        door(cv, x + W - 13, y + H - 10, 7);
        break;
    }
    case BType::Lumber: {
        cottage(cv, x + 1, y + 3, 20, H - 6, roof, night, winter, 11);
        door(cv, x + 8, y + H - 10, 7);
        logPile(cv, x + 20, y + H - 1, pileLevel(w, Res::Logs, 1 + w.count(BType::Lumber), 3.f, 6));
        // stump with axe
        cv.rect(x + 23, y + 8, 6, 4, TRUNK); cv.rect(x + 23, y + 7, 6, 1, WOOD_L);
        cv.line(x + 26, y + 7, x + 28, y + 2, WOOD); cv.rect(x + 27, y + 1, 3, 2, ROCK_LL);
        break;
    }
    case BType::Sawmill: {
        cottage(cv, x + 1, y + 6, W - 2, H - 8, roof, night, winter, 9);
        door(cv, x + 5, y + H - 10, 7);
        // big saw blade with turning teeth
        int cx = x + W - 9, cy = y + 7;
        bool on = b.staffed > 0 && b.connected && w.store[(int)Res::Logs] >= 9.f;
        float a0 = on ? frame * 0.35f : 0.f;
        for (int yy = -5; yy <= 5; yy++) for (int xx = -5; xx <= 5; xx++) {
            float d = std::sqrt((float)(xx * xx + yy * yy));
            if (d <= 4.5f) cv.put(cx + xx, cy + yy, d > 3.5f ? ROCK_D : (xx + yy < 0 ? ROCK_LL : ROCK_L));
        }
        for (int k = 0; k < 8; k++) { float a = a0 + k * 0.785f; cv.put(cx + (int)std::lround(std::cos(a) * 5.5f), cy + (int)std::lround(std::sin(a) * 5.5f), ROCK_LL); }
        cv.rect(cx - 1, cy - 1, 2, 2, OUTLINE);
        plankPile(cv, x + W - 15, y + H - 2, pileLevel(w, Res::Planks, 1 + w.count(BType::Sawmill), 3.f, 6), 12);
        break;
    }
    case BType::Fisher: {
        cottage(cv, x + 1, y + 4, 21, H - 6, roof, night, winter, 11);
        door(cv, x + 8, y + H - 10, 7);
        // drying rack with fish
        cv.line(x + 24, y + 6, x + 24, y + H - 3, WOOD_D); cv.line(x + 30, y + 6, x + 30, y + H - 3, WOOD_D);
        cv.line(x + 23, y + 7, x + 31, y + 7, WOOD);
        int nf = pileLevel(w, Res::Fish, w.count(BType::Fisher) + w.count(BType::Bakery) + 1, 8.f, 8);
        for (int i = 0; i < std::min(3, nf); i++) { int fx = x + 25 + i * 2; cv.rect(fx, y + 8, 1, 4, 0x8fd3ff); cv.put(fx, y + 12, 0x4d9be6); }
        crate(cv, x + 23, y + H - 1, std::max(0, nf - 3), true);
        break;
    }
    case BType::Farm: {
        float g = b.grow;
        uint32_t soil = snowy(SOIL, winter, 0.6f);
        cv.rect(x + 1, y + 1, W - 2, H - 2, soil);
        for (int j = y + 2; j < y + H - 2; j++) for (int i = x + 2; i < x + W - 2; i++) if ((hashc(i, j, 4) & 15) == 0) cv.put(i, j, SOIL_D);
        // crop rows: stalk height and colour follow growth
        if (!winter && g > 0.02f) {
            int hgt = 1 + (int)(g * 4.f);
            uint32_t stalk = g > 0.75f ? RIPE_D : (g > 0.4f ? mixc(SPROUT, RIPE, (g - 0.4f) * 2.5f) : SPROUT);
            for (int row = y + 7; row < y + H - 3; row += 5) {
                for (int i = x + 3; i < x + W - 18; i += 2) {
                    int hh = hgt - (int)(hashc(i, row) & 1);
                    for (int k = 0; k < hh; k++) cv.put(i, row - k, k == hh - 1 && g > 0.75f ? RIPE : stalk);
                }
                cv.rect(x + 3, row + 1, W - 21, 1, SOIL_D);
            }
            for (int row = y + 7; row < y + 26; row += 5)
                for (int i = x + W - 18; i < x + W - 3; i += 2) {
                    int hh = hgt - (int)(hashc(i, row) & 1);
                    for (int k = 0; k < hh; k++) cv.put(i, row - k, k == hh - 1 && g > 0.75f ? RIPE : stalk);
                }
        } else for (int row = y + 7; row < y + H - 3; row += 5) cv.rect(x + 3, row + 1, W - 6, 1, SOIL_D);
        // fence posts and rails
        for (int i = x; i < x + W; i++) { cv.put(i, y, WOOD_L); cv.put(i, y + H - 1, WOOD); }
        for (int j = y; j < y + H; j++) { cv.put(x, j, WOOD_L); cv.put(x + W - 1, j, WOOD); }
        for (int i = x; i < x + W; i += 6) { cv.rect(i, y - 1, 1, 3, WOOD_D); cv.rect(i, y + H - 2, 1, 3, WOOD_D); }
        // barn in the corner
        cottage(cv, x + W - 17, y + H - 18, 15, 15, roof, night, winter, 7, 0xc75b39);
        cv.rect(x + W - 12, y + H - 9, 5, 6, DOOR); cv.line(x + W - 12, y + H - 9, x + W - 8, y + H - 4, WOOD_L);
        sacks(cv, x + W - 33, y + H - 2, pileLevel(w, Res::Wheat, w.count(BType::Farm), 4.f, 6), true);   // harvested sheaves
        // scarecrow
        if (!winter) { int sx = x + 10, sy = y + 30; cv.line(sx, sy, sx, sy - 8, WOOD_D); cv.line(sx - 3, sy - 6, sx + 3, sy - 6, WOOD_D); cv.rect(sx - 1, sy - 10, 3, 3, RIPE); cv.rect(sx - 2, sy - 11, 5, 1, 0x7a4a2a); }
        break;
    }
    case BType::Mill: {
        int cx = x + W / 2, top = y + 9;
        cv.shadow(cx + 4, y + H - 1, 9, 3, 0.3f);
        // tapering stone tower
        for (int j = top; j < y + H - 1; j++) {
            int half = 4 + (j - top) * 4 / (y + H - top);
            for (int i = -half; i <= half; i++) {
                uint32_t c = i < -half + 2 ? WALL : (i > half - 3 ? WALL_D : 0xf0e2c8);
                if (((j - top) % 4 == 0) && ((i + (j / 4) * 2) % 5 == 0)) c = WALL_D;
                if (i == -half || i == half) c = OUTLINE;
                cv.put(cx + i, j, c);
            }
        }
        cv.line(cx - 7, y + H - 1, cx + 7, y + H - 1, OUTLINE);
        // conical cap
        for (int r = 0; r < 6; r++) for (int i = -(r + 1); i <= r + 1; i++) cv.put(cx + i, top - 6 + r, i == -(r + 1) || i == r + 1 ? OUTLINE : (i < 0 ? lighten(roof) : roof));
        door(cv, cx - 2, y + H - 9, 7);
        window(cv, cx - 2, top + 5, night);
        bool turning = b.staffed > 0 && b.connected && w.store[(int)Res::Wheat] >= 2.f;
        float a0 = turning ? frame * 0.06f : 0.5f;
        int hx = cx, hy = top - 1;
        for (int k = 0; k < 4; k++) {
            float a = a0 + k * 1.5708f, ca = std::cos(a), sa = std::sin(a);
            for (int i = 1; i <= 13; i++) cv.put(hx + (int)std::lround(ca * i), hy + (int)std::lround(sa * i), WOOD_D);
            // lattice sail on one side of each arm
            for (int i = 4; i <= 13; i++)
                for (int s = 1; s <= 3; s++) {
                    int px = hx + (int)std::lround(ca * i - sa * s), py = hy + (int)std::lround(sa * i + ca * s);
                    cv.put(px, py, ((i + s) % 3 == 0) ? WOOD : 0xfdf7ed);
                }
        }
        cv.rect(hx - 1, hy - 1, 3, 3, OUTLINE);
        sacks(cv, cx + 5, y + H, pileLevel(w, Res::Flour, w.count(BType::Mill), 3.f, 3), false);
        break;
    }
    case BType::Bakery: {
        cottage(cv, x + 1, y + 6, W - 2, H - 8, roof, night, winter, 11);
        // brick chimney + smoke when baking
        cv.rect(x + 5, y, 5, 9, 0x8a4836);
        for (int j = 0; j < 9; j += 2) cv.rect(x + 5 + (j / 2 % 2) * 2, y + j, 1, 1, 0x6e2727);
        cv.outlineRect(x + 4, y - 1, 7, 10, OUTLINE);
        bool baking = b.staffed > 0 && b.connected && w.store[(int)Res::Flour] >= 1.f;
        if (baking) smoke(cv, x + 7, y - 3, frame, seed);
        window(cv, x + 6, y + 21, night);
        door(cv, x + W - 12, y + H - 10, 7);
        // hanging bread sign
        cv.line(x + W - 4, y + 15, x + W, y + 15, WOOD_D);
        cv.rect(x + W - 4, y + 16, 5, 4, 0xcd683d); cv.rect(x + W - 3, y + 16, 3, 1, SAND_L);
        int nb = pileLevel(w, Res::Bread, w.count(BType::Bakery) + 1, 8.f, 6);
        if (nb > 0) crate(cv, x + 2, y + H - 1, nb, false);
        break;
    }
    case BType::Well: {
        cv.shadow(x + 10, y + 14, 6, 2);
        // stone ring
        for (int yy = -4; yy <= 4; yy++) for (int xx = -6; xx <= 6; xx++) {
            float d = std::sqrt(xx * xx / 36.f + yy * yy / 16.f);
            if (d > 1.f) continue;
            uint32_t c = d < 0.62f ? WATER : ((xx + yy) < 0 ? ROCK_L : ROCK);
            if (d < 0.62f && xx < -1 && yy < 0) c = WATER_HI;
            if (d > 0.88f) c = OUTLINE;
            cv.put(x + 8 + xx, y + 10 + yy, c);
        }
        // posts and little roof
        cv.rect(x + 2, y + 2, 1, 8, WOOD_D); cv.rect(x + 13, y + 2, 1, 8, WOOD_D);
        cv.line(x + 3, y + 4, x + 12, y + 4, WOOD);
        cv.rect(x + 7, y + 5, 2, 3, WOOD_L);   // bucket rope
        for (int r = 0; r < 3; r++) cv.rect(x + 3 - r, y - 1 + r, 10 + r * 2, 1, r == 0 ? lighten(roof) : roof);
        cv.outlineRect(x, y - 2, 16, 5, OUTLINE);
        break;
    }
    case BType::Healer: {
        cottage(cv, x + 1, y + 6, W - 2, H - 8, roof, night, winter, 11, 0xfdf7ed);
        // green cross banner
        cv.rect(x + W / 2 - 4, y - 2, 8, 10, 0xfdf7ed); cv.outlineRect(x + W / 2 - 5, y - 3, 10, 12, OUTLINE);
        cv.rect(x + W / 2 - 1, y - 1, 2, 8, 0x1ebc73); cv.rect(x + W / 2 - 3, y + 2, 6, 2, 0x1ebc73);
        window(cv, x + 5, y + 21, night);
        door(cv, x + W - 12, y + H - 10, 7);
        // herb pots
        for (int i = 0; i < 2; i++) { cv.rect(x + 3 + i * 5, y + H - 4, 4, 3, 0xcd683d); cv.rect(x + 3 + i * 5, y + H - 6, 4, 2, 0x5ac54f); }
        break;
    }
    case BType::Tower: {
        int tx = x + 7, tw = W - 14, top = y - 14;
        cv.shadow(tx + tw / 2 + 4, y + H - 1, tw / 2 + 3, 3, 0.35f);
        for (int j = top + 5; j < y + H - 1; j++)
            for (int i = 0; i < tw; i++) {
                uint32_t c = i < 3 ? ROCK_LL : (i > tw - 4 ? ROCK : ROCK_L);
                int row = (j - top) / 3;
                if ((j - top) % 3 == 0 || ((i + (row & 1) * 3) % 6 == 0)) c = darken(c, 0.15f);
                cv.put(tx + i, j, c);
            }
        // crenellations
        for (int i = -1; i < tw + 1; i += 4) { cv.rect(tx + i, top, 3, 5, ROCK_L); cv.outlineRect(tx + i - 1, top - 1, 5, 6, OUTLINE); }
        cv.rect(tx - 1, top + 4, tw + 2, 2, roof);
        cv.line(tx - 1, top + 6, tx - 1, y + H - 1, OUTLINE); cv.line(tx + tw, top + 6, tx + tw, y + H - 1, OUTLINE);
        cv.line(tx - 1, y + H - 1, tx + tw, y + H - 1, OUTLINE);
        cv.rect(tx + tw / 2 - 1, top + 10, 2, 5, OUTLINE);   // arrow slit
        door(cv, tx + tw / 2 - 2, y + H - 10, 8);
        if (b.staffed) {   // archer on top
            int ax = tx + tw / 2;
            cv.rect(ax - 1, top - 5, 3, 3, SKIN); cv.rect(ax - 1, top - 6, 3, 1, 0x4c3e24);
            cv.rect(ax - 1, top - 2, 3, 2, ROOF_SAFE);
            cv.line(ax + 3, top - 6, ax + 3, top - 1, WOOD_D);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- people
// Sprite grids: '.' empty, h hair, s skin, e eye, c shirt, C shirt shade, p pants, k shoe,
// m helmet, w horn, b beard, a axe head, t axe handle, o carried good
const char* ADULT[2][12] = {
    {"..hhh..", ".hhhhh.", ".hsese.", ".sssss.", "..sss..", ".ccccc.", "cccccCc", "cccccCc", "s.ppp.s", "..p.p..", "..p.p..", ".kk.kk."},
    {"..hhh..", ".hhhhh.", ".hsese.", ".sssss.", "..sss..", ".ccccc.", "cccccCc", "cccccCc", "s.ppp.s", ".pp.pp.", ".p...p.", "kk...kk"}};
const char* KID[2][9] = {
    {".hhh.", "hsese", ".sss.", ".ccc.", "ccccc", "s.p.s", ".p.p.", ".k.k.", "....."},
    {".hhh.", "hsese", ".sss.", ".ccc.", "ccccc", "s.p.s", "p...p", "k...k", "....."}};
const char* RAIDERS[2][12] = {
    {"w.mmm.w", ".mmmmm.", ".msesm.", ".sssss.", ".bbbbb.", "ccbbbcc", "cccccCa", "cccccCt", "s.ppp.t", "..p.p..", "..p.p..", ".kk.kk."},
    {"w.mmm.w", ".mmmmm.", ".msesm.", ".sssss.", ".bbbbb.", "ccbbbcc", "cccccCa", "cccccCt", "s.ppp.t", ".pp.pp.", ".p...p.", "kk...kk"}};

void sprite(Canvas& cv, int left, int top, const char* const* rows, int nrows, uint32_t hair, uint32_t shirt, uint32_t skin, bool flip) {
    int wdt = (int)std::strlen(rows[0]);
    auto colorAt = [&](int x, int y, uint32_t& c) -> bool {
        if (x < 0 || y < 0 || x >= wdt || y >= nrows) return false;
        char ch = rows[y][flip ? wdt - 1 - x : x];
        switch (ch) {
        case 'h': c = hair; return true; case 's': c = skin; return true; case 'e': c = OUTLINE; return true;
        case 'c': c = shirt; return true; case 'C': c = darken(shirt, 0.3f); return true;
        case 'p': c = PANTS; return true; case 'k': c = SHOE; return true;
        case 'm': c = HELM; return true; case 'w': c = 0xfdf7ed; return true; case 'b': c = 0x7a4a2a; return true;
        case 'a': c = ROCK_LL; return true; case 't': c = WOOD; return true;
        default: return false;
        }
    };
    // outline first (any empty cell touching a filled one), then fill
    for (int y = -1; y <= nrows; y++)
        for (int x = -1; x <= wdt; x++) {
            uint32_t c;
            if (colorAt(x, y, c)) continue;
            bool touch = false;
            for (int k = 0; k < 4 && !touch; k++) { static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1}; touch = colorAt(x + dx[k], y + dy[k], c); }
            if (touch) cv.put(left + x, top + y, OUTLINE);
        }
    for (int y = 0; y < nrows; y++) for (int x = 0; x < wdt; x++) { uint32_t c; if (colorAt(x, y, c)) cv.put(left + x, top + y, c); }
}
} // namespace (helpers continue below)

namespace {
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

// Speech bubble with a 2x-scaled 5x4 icon.
// k scales the whole bubble so it keeps its on-screen size when the camera zooms out.
void bubble(Canvas& cv, int cx, int y, int frame, int kind, int k) {
    int bob = (frame / 15) % 2;
    int bx = cx - 7 * k, by = y - (15 + bob) * k;
    auto r = [&](int x, int y2, int w, int h, uint32_t c) { cv.rect(bx + x * k, by + y2 * k, w * k, h * k, c); };
    r(0, 0, 15, 12, OUTLINE);
    r(1, 1, 13, 10, 0xffffff);
    r(1, 9, 13, 2, 0xdcd6e0);
    r(7, 12, 1, 1, OUTLINE); r(6, 12, 1, 1, OUTLINE); r(7, 13, 1, 1, OUTLINE);
    auto p = [&](int x, int y2, uint32_t c) { r(2 + x * 2, 2 + y2 * 2, 2, 2, c); };
    switch (kind) {
    case 0: for (int i = 0; i < 5; i++) for (int j = 1; j < 4; j++) p(i, j, 0xcd683d); for (int i = 1; i < 4; i++) p(i, 0, SAND_L); p(1, 2, RIPE); p(3, 2, RIPE); break;
    case 1: for (int i = 0; i < 5; i++) { p(i, 1, TRUNK); p(i, 3, TRUNK_L); } p(2, 0, FIRE1); p(1, 2, FIRE2); p(3, 2, FIRE1); break;
    case 2: for (int i = 0; i < 5; i++) for (int j = 0; j < 3; j++) p(i, j, 0xe83b3b); p(1, 1, 0xffffff); p(2, 1, 0xffffff); p(3, 1, 0xffffff); p(2, 3, TRUNK); break;
    case 3: for (int i = 1; i < 4; i++) { p(i, 1, RIPE); p(i, 2, RIPE_D); } p(2, 0, RIPE); p(1, 3, 0xcd683d); p(3, 3, 0xcd683d); break;
    case 4: for (int i = 1; i < 4; i++) for (int j = 1; j < 4; j++) p(i, j, 0xe8e0d0); p(2, 0, WOOD); p(2, 2, 0xc4ae8c); break;
    case 5: for (int i = 0; i < 5; i++) { p(i, 1, TRUNK); p(i, 2, TRUNK_L); } p(0, 1, WOOD_L); p(0, 2, RIPE_D); break;
    case 6: p(2, 0, SKIN); for (int i = 1; i < 4; i++) p(i, 1, 0x4d9be6); p(1, 2, PANTS); p(3, 2, PANTS); p(1, 3, SHOE); p(3, 3, SHOE); break;
    }
}

void circle(Canvas& cv, float cx, float cy, float r, uint32_t col, int frame) {
    int steps = std::max(32, (int)(r * 6));
    for (int i = 0; i < steps; i++) {
        if ((i + frame / 3) % 4 == 0) continue;
        float a = i * 6.2831853f / steps;
        int x = (int)std::lround(cx + std::cos(a) * r), y = (int)std::lround(cy + std::sin(a) * r);
        cv.put(x, y, col); cv.put(x + 1, y, col);
    }
}
} // namespace

void drawWorld(const World& w, uint32_t* px, int pitchPx, int frame, const Overlay& ov) {
    Canvas cv{px, pitchPx};
    cv.cx0 = std::clamp(ov.vx0, 0, VIEW_W); cv.cy0 = std::clamp(ov.vy0, 0, VIEW_H);
    cv.cx1 = std::clamp(ov.vx1, 0, VIEW_W); cv.cy1 = std::clamp(ov.vy1, 0, VIEW_H);
    int tx0 = std::max(0, cv.cx0 / T - 1), ty0 = std::max(0, cv.cy0 / T - 1);
    int tx1 = std::min(MAP_W - 1, cv.cx1 / T + 1), ty1 = std::min(MAP_H - 1, cv.cy1 / T + 2);
    bool winter = w.season() == Season::Winter;
    float t = w.dayFrac();
    bool lamps = t > 0.72f || t < 0.12f;

    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            const Cell& c = w.at(tx, ty);
            int ox = tx * T, oy = ty * T;
            switch (c.t) {
            case Tile::Deep: waterTile(cv, w, ox, oy, tx, ty, frame, true); break;
            case Tile::Water: waterTile(cv, w, ox, oy, tx, ty, frame, false); break;
            case Tile::Sand: sandTile(cv, ox, oy, winter); break;
            case Tile::Mountain: rockTile(cv, c, ox, oy, tx, ty, winter); break;
            case Tile::Road: roadTile(cv, w, ox, oy, tx, ty, winter); break;
            case Tile::Ash: ashTile(cv, c, ox, oy, tx, ty, frame); break;
            default: grassTile(cv, ox, oy, tx, ty, winter); break;
            }
        }

    // y-sorted objects: trees and buildings by their bottom row (taller things overlap what is behind)
    struct Obj { int bottom; int kind; int a, b; };
    std::vector<Obj> objs;
    for (int ty = ty0; ty <= std::min(MAP_H - 1, ty1 + 2); ty++)
        for (int tx = tx0; tx <= tx1; tx++) if (w.at(tx, ty).t == Tile::Forest) objs.push_back({ty * 2, 0, tx, ty});
    for (int i = 0; i < (int)w.buildings.size(); i++) {
        const Building& b = w.buildings[i];
        if (!b.alive) continue;
        const BInfo& in = binfo(b.type);
        if (b.x + in.w < tx0 || b.x > tx1 + 1 || b.y + in.h < ty0 || b.y > ty1 + 2) continue;
        objs.push_back({(b.y + in.h - 1) * 2 + 1, 1, i, 0});
    }
    std::stable_sort(objs.begin(), objs.end(), [](const Obj& a, const Obj& b) { return a.bottom < b.bottom; });
    for (const Obj& o : objs) {
        if (o.kind == 0) drawTree(cv, w.at(o.a, o.b), o.a, o.b, winter, frame);
        else {
            const Building& b = w.buildings[o.a];
            int why = idleReason(w, b);
            cv.desat = (why >= 2) ? 0.55f : 0.f;   // stuck buildings go grey
            drawBuilding(cv, w, b, frame, lamps, winter, o.a * 13);
            cv.desat = 0.f;
        }
    }
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (w.at(tx, ty).fire > 0.f) drawFire(cv, tx * T, ty * T, hashc(tx, ty), frame);

    // people
    for (int i = 0; i < (int)w.villagers.size(); i++) {
        const Villager& v = w.villagers[i];
        if (!v.alive) continue;
        bool moving = v.pathPos < v.path.size();
        int f = moving ? (frame / 5 + i) % 2 : 0;
        bool flip = false;
        if (moving) { int ni = v.path[v.pathPos]; flip = (ni % MAP_W) < v.x - 0.01f; }
        uint32_t hair = v.age > 50.f ? 0xdcd6e0 : HAIR[hashc(i, 3) % 5];
        uint32_t shirt = v.sick ? SICK : (v.work >= 0 ? roofOf(w.buildings[v.work].type) : (v.age < 6.f ? 0xf5a097 : 0x4d9be6));
        uint32_t skin = v.sick ? mixc(SKIN, SICK, 0.5f) : (hashc(i, 9) % 3 == 0 ? SKIN_D : SKIN);
        int fx = (int)std::lround(v.x * T) + 8, fy = (int)std::lround(v.y * T) + 14;
        if (fx < cv.cx0 - 10 || fx > cv.cx1 + 10 || fy < cv.cy0 - 16 || fy > cv.cy1 + 16) continue;
        cv.shadow(fx, fy, 3, 1, 0.3f);
        if (v.age < 6.f) sprite(cv, fx - 2, fy - 8, KID[f], 8, hair, shirt, skin, flip);
        else sprite(cv, fx - 3, fy - 12, ADULT[f], 12, hair, shirt, skin, flip);
        if (v.carrying && v.work >= 0) {   // carried good above the head
            bool logs = w.buildings[v.work].type == BType::Lumber;
            int cxp = fx - 2, cyp = fy - (v.age < 6.f ? 12 : 16);
            cv.rect(cxp - 1, cyp - 1, 6, 4, OUTLINE);
            cv.rect(cxp, cyp, 4, 2, logs ? WOOD_L : 0x8fd3ff);
            if (logs) cv.put(cxp, cyp, RIPE_D); else cv.put(cxp + 3, cyp + 1, 0x4d9be6);
        }
    }
    for (int i = 0; i < (int)w.raiders.size(); i++) {
        const Raider& r = w.raiders[i];
        if (!r.alive) continue;
        int fx = (int)std::lround(r.x * T) + 8, fy = (int)std::lround(r.y * T) + 14;
        cv.shadow(fx, fy, 3, 1, 0.3f);
        sprite(cv, fx - 3, fy - 12, RAIDERS[(frame / 4 + i) % 2], 12, HELM, RAIDER, SKIN_D, false);
        if (r.loot > 0) { cv.rect(fx + 3, fy - 8, 4, 4, OUTLINE); cv.rect(fx + 4, fy - 7, 2, 2, RIPE); }
    }
    for (const Arrow& a : w.arrows) {
        int age = w.ticks - a.born;
        if (age < 0 || age > 3) continue;
        float k0 = age / 4.f, k1 = (age + 1) / 4.f;
        float x0 = a.x0 * T, y0 = a.y0 * T - 14, x1 = a.x1 * T + 8, y1 = a.y1 * T + 6;
        cv.line(x0 + (x1 - x0) * k0, y0 + (y1 - y0) * k0, x0 + (x1 - x0) * k1, y0 + (y1 - y0) * k1, 0xffffff);
    }

    // day/night + drought tint on the visible region
    float night = 0.f;
    if (t > 0.70f) night = std::min(1.f, (t - 0.70f) / 0.12f);
    else if (t < 0.12f) night = 1.f - t / 0.12f;
    night *= 0.5f;
    float dry = w.droughtDays > 0 ? 0.12f : 0.f;
    if (night > 0.f || dry > 0.f)
        for (int y = cv.cy0; y < cv.cy1; y++)
            for (int x = cv.cx0; x < cv.cx1; x++) {
                uint32_t& p = px[y * pitchPx + x];
                if (p == LIT || p == FIRE1 || p == FIRE2 || p == FIRE3) continue;
                uint32_t c = p;
                if (dry > 0.f) c = mixc(c, 0xfbb954, dry);
                if (night > 0.f) c = mixc(c, 0x1a1b33, night);
                p = c;
            }

    for (const Obj& o : objs) {
        if (o.kind != 1) continue;
        const Building& b = w.buildings[o.a];
        int why = idleReason(w, b);
        if (why < 0) continue;
        const BInfo& in = binfo(b.type);
        bubble(cv, b.x * T + in.w * T / 2, b.y * T + (b.type == BType::Tower ? -16 : -1), frame + o.a * 7, why, ov.markScale);
    }

    if (ov.selected >= 0 && ov.selected < (int)w.buildings.size() && w.buildings[ov.selected].alive) {
        const Building& b = w.buildings[ov.selected];
        const BInfo& in = binfo(b.type);
        cv.outlineRect(b.x * T - 1, b.y * T - 1, in.w * T + 2, in.h * T + 2, 0xffffff);
        if (in.radius > 0.f) circle(cv, (b.x + in.w / 2.f) * T, (b.y + in.h / 2.f) * T, in.radius * T, 0xffffff, frame);
    }
    for (int i : ov.roadTiles) {
        int x = i % MAP_W, y = i / MAP_W;
        bool ok = w.canRoad(x, y) || w.at(x, y).t == Tile::Road;
        cv.blendRect(x * T, y * T, T, T, ok ? 0x9cdb43 : 0xe83b3b, 0.45f);
    }
    if (ov.demolishX >= 0 && w.inside(ov.demolishX, ov.demolishY)) {
        int bi = w.buildingAt(ov.demolishX, ov.demolishY);
        int x = ov.demolishX, y = ov.demolishY, bw = 1, bh = 1;
        if (bi >= 0) { x = w.buildings[bi].x; y = w.buildings[bi].y; bw = binfo(w.buildings[bi].type).w; bh = binfo(w.buildings[bi].type).h; }
        cv.blendRect(x * T, y * T, bw * T, bh * T, 0xe83b3b, 0.4f);
        cv.outlineRect(x * T, y * T, bw * T, bh * T, 0xe83b3b);
    }
    if (ov.ghost >= 0) {
        BType bt = (BType)ov.ghost;
        const BInfo& in = binfo(bt);
        uint32_t tint = ov.ghostOk ? 0x9cdb43 : 0xe83b3b;
        float gcx = ov.gx + in.w / 2.f, gcy = ov.gy + in.h / 2.f;
        if (in.radius > 0.f) {
            float r = in.radius;
            bool service = bt == BType::Well || bt == BType::Healer || bt == BType::Tower;
            if (service)
                for (const Building& b : w.buildings) {
                    if (!b.alive || (b.type != BType::House && b.type != BType::Hall)) continue;
                    const BInfo& bi = binfo(b.type);
                    if (std::hypot(b.x + bi.w / 2.f - gcx, b.y + bi.h / 2.f - gcy) <= r) {
                        cv.outlineRect(b.x * T - 1, b.y * T - 1, bi.w * T + 2, bi.h * T + 2, 0xf9c22b);
                        cv.outlineRect(b.x * T, b.y * T, bi.w * T, bi.h * T, 0xf9c22b);
                    }
                }
            if (bt == BType::Lumber || bt == BType::Fisher) {
                Tile want = bt == BType::Lumber ? Tile::Forest : Tile::Water;
                for (int y = (int)(gcy - r); y <= gcy + r; y++)
                    for (int x = (int)(gcx - r); x <= gcx + r; x++)
                        if (w.inside(x, y) && w.at(x, y).t == want && std::hypot(x + 0.5f - gcx, y + 0.5f - gcy) <= r)
                            cv.blendRect(x * T + 4, y * T + 4, 8, 8, 0xffffff, 0.35f);
            }
            circle(cv, gcx * T, gcy * T, r * T, ov.ghostOk ? 0xffffff : 0xe83b3b, frame);
        }
        Building fake; fake.type = bt; fake.x = ov.gx; fake.y = ov.gy; fake.connected = true; fake.staffed = 1; fake.grow = 0.6f;
        drawBuilding(cv, w, fake, frame, false, winter, 0);
        cv.blendRect(ov.gx * T, ov.gy * T, in.w * T, in.h * T, tint, 0.35f);
        cv.outlineRect(ov.gx * T, ov.gy * T, in.w * T, in.h * T, tint);
    }
}
