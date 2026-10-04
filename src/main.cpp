// A2DVillageSim: keep your civilization alive as long as you can.
// Controls: 1-4 pick a power, left click casts it, wheel zoom, WASD/arrows or right-drag pan,
// H back to the village, Space pause, +/- speed, R restart.
#include <SDL3/SDL.h>
#include "render.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace sim;

static int loadBest() {
    std::ifstream f("best_score.txt");
    int b = 0; if (f) f >> b; return b;
}
static void saveBest(int b) { std::ofstream f("best_score.txt"); f << b; }

struct Game {
    std::unique_ptr<World> world;
    int power = 0;
    int speed = 1;          // ticks per 0.1 s
    bool paused = false;
    int best = 0;
    bool bestSaved = false;
    uint32_t seed = 1;
    int zoom = 2;           // world pixels are drawn zoom x zoom (whole steps keep pixels crisp)
    float camX = 0, camY = 0; // top-left of the view in world pixels
    // Logical screen, recomputed from the window so any phone/desktop aspect works.
    int scrW = 480, scrH = 280, hudH = 24, viewW = 480, viewH = 256;
    int minZoom() const {   // never show past the map edge
        int zx = (viewW + VIEW_W - 1) / VIEW_W, zy = (viewH + VIEW_H - 1) / VIEW_H;
        return std::max(1, std::max(zx, zy));
    }
    int maxZoom() const { return minZoom() + 4; }
    void centerOnHall() {
        camX = world->hallX * TILE_PX + 2 - viewW / (2.f * zoom);
        camY = world->hallY * TILE_PX + 2 - viewH / (2.f * zoom);
        clampCam();
    }
    void clampCam() {
        zoom = std::clamp(zoom, minZoom(), maxZoom());
        float vw = (float)viewW / zoom, vh = (float)viewH / zoom;
        camX = std::clamp(camX, 0.f, std::max(0.f, VIEW_W - vw));
        camY = std::clamp(camY, 0.f, std::max(0.f, VIEW_H - vh));
    }
    // logical screen -> world pixel
    void toWorld(float lx, float ly, float& wx, float& wy) const { wx = camX + lx / zoom; wy = camY + ly / zoom; }
    void setZoom(int z, float lx, float ly) {
        float wx, wy; toWorld(lx, ly, wx, wy);
        zoom = std::clamp(z, minZoom(), maxZoom());
        camX = wx - lx / zoom; camY = wy - ly / zoom;
        clampCam();
    }
    int lastItemCount = 0;
    bool layoutDirty() { int n = 7 + (world->sickCount() > 0); bool d = n != lastItemCount; lastItemCount = n; return d; }
    SDL_FRect powerBtn[(int)Power::Count]{};
    SDL_FRect speedBtn{};
    int safeTop = 0, safeBottom = 0;   // notch / home-indicator insets in logical px
    void cycleSpeed() { if (paused) { paused = false; speed = 1; } else if (speed >= 8) paused = true; else speed *= 2; }   // HUD hit boxes (tap to select on phones)
    void restart(uint32_t s) { seed = s; world = std::make_unique<World>(s, true); bestSaved = false; centerOnHall(); }
};

// 1px dark outline so text reads in sunlight over any terrain.
static void text(SDL_Renderer* r, float x, float y, uint32_t rgb, const std::string& s) {
    SDL_SetRenderDrawColor(r, 0x14, 0x10, 0x18, 255);
    const float o[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& d : o) SDL_RenderDebugText(r, x + d[0], y + d[1], s.c_str());
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
}
static void rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t rgb, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
    SDL_FRect fr{x, y, w, h};
    SDL_RenderFillRect(r, &fr);
}

// Flow layout: items wrap onto new rows when the screen is narrow (portrait phones).
struct Flow {
    float x0, x, y, maxX, rowH;
    Flow(float left, float top, float right, float rh) : x0(left), x(left), y(top), maxX(right), rowH(rh) {}
    SDL_FRect place(float w, float gap) {
        if (x > x0 && x + w > maxX) { x = x0; y += rowH; }
        SDL_FRect r{x, y, w, rowH};
        x += w + gap;
        return r;
    }
    float bottom() const { return y + rowH; }
};

struct HudItem { std::string s; uint32_t col; int power; };

static std::vector<HudItem> hudItems(const Game& g, int& statCount) {
    const World& w = *g.world;
    std::vector<HudItem> it;
    it.push_back({"Day " + std::to_string(w.day() + 1) + " " + seasonName(w.season()), 0xffffff, -1});
    it.push_back({"Pop " + std::to_string(w.population()), 0xc7dcd0, -1});
    it.push_back({"Food " + std::to_string((int)w.food), w.food < w.population() * 2 ? 0xf9c22b : 0xc7dcd0, -1});
    it.push_back({"Wood " + std::to_string((int)w.wood), 0xc7dcd0, -1});
    if (w.sickCount() > 0) it.push_back({"Sick " + std::to_string(w.sickCount()), 0x9cdb43, -1});
    it.push_back({"Mana " + std::to_string((int)w.mana), 0xa884f3, -1});
    statCount = (int)it.size();
    for (int i = 0; i < (int)Power::Count; i++) {
        const PowerInfo& pi = powerInfo((Power)i);
        it.push_back({std::to_string(i + 1) + " " + pi.name + " " + std::to_string(pi.cost), 0, i});
    }
    it.push_back({std::string(g.paused ? "PAUSED " : "") + "x" + std::to_string(g.speed), g.paused ? 0xf9c22b : 0x7f708a, -2});
    return it;
}

// Lays out (and optionally draws) the HUD; returns its height.
// Stats wrap in small text rows; powers are finger-sized buttons that also wrap.
static int layoutHud(SDL_Renderer* ren, Game& g, bool draw) {
    int statCount;
    auto items = hudItems(g, statCount);
    const float rowH = 11.f, btnH = 28.f;   // ~48dp thumb targets at typical phone scales
    float top = (float)(g.scrH - g.hudH) + 3.f;
    Flow f(3.f, top, (float)g.scrW - 3.f, rowH);
    const World& w = *g.world;
    for (int i = 0; i < statCount; i++) {
        const HudItem& h = items[i];
        SDL_FRect r = f.place(8.f * h.s.size(), 12.f);
        if (draw) text(ren, r.x, r.y + 1, h.col, h.s);
    }
    Flow b(3.f, f.bottom() + 2.f, (float)g.scrW - 3.f, btnH + 4.f);
    for (int i = statCount; i < (int)items.size(); i++) {
        const HudItem& h = items[i];
        float tw = 8.f * h.s.size();
        SDL_FRect r = b.place(tw + 12.f, 4.f);
        SDL_FRect hit{r.x, r.y, r.w, btnH};
        if (h.power >= 0) g.powerBtn[h.power] = hit; else g.speedBtn = hit;
        if (!draw) continue;
        bool sel = h.power >= 0 && g.power == h.power;
        bool afford = h.power < 0 || w.mana >= powerInfo((Power)h.power).cost;
        rect(ren, r.x, r.y, r.w, btnH, sel ? 0x6b3e75 : 0x3e3546);
        if (sel) rect(ren, r.x, r.y + btnH - 2, r.w, 2, 0xa884f3);
        uint32_t col = h.power < 0 ? h.col : (afford ? (sel ? 0xffffff : 0x9babb2) : 0x7a3045);
        text(ren, r.x + 5, r.y + (btnH - 8) / 2, col, h.s);
    }
    return (int)(b.bottom() - top) + 3 + g.safeBottom;
}

static void drawHud(SDL_Renderer* ren, Game& g) {
    const World& w = *g.world;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    rect(ren, 0, (float)g.viewH, (float)g.scrW, (float)g.hudH, 0x2e222f);
    // mana: thin bar on the HUD's top edge
    rect(ren, 0, (float)g.viewH, (float)g.scrW, 2, 0x323353);
    rect(ren, 0, (float)g.viewH, g.scrW * w.mana / 100.f, 2, 0x905ea9);
    layoutHud(ren, g, true);

    // Off-screen danger arrows: orange fire, green plague, red raiders.
    {
        struct Acc { float x = 0, y = 0; int n = 0; uint32_t col; } acc[3];
        acc[0].col = 0xfb6b1d; acc[1].col = 0x9cdb43; acc[2].col = 0xe83b3b;
        float vx0 = g.camX, vy0 = g.camY, vx1 = g.camX + (float)g.viewW / g.zoom, vy1 = g.camY + (float)g.viewH / g.zoom;
        auto off = [&](float px, float py) { return px < vx0 || px >= vx1 || py < vy0 || py >= vy1; };
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (w.at(x, y).fire > 0.f) {
                    float px = x * TILE_PX + 2.f, py = y * TILE_PX + 2.f;
                    if (off(px, py)) { acc[0].x += px; acc[0].y += py; acc[0].n++; }
                }
        for (const Villager& v : w.villagers)
            if (v.alive && v.sick) {
                float px = v.x * TILE_PX + 2.f, py = v.y * TILE_PX + 2.f;
                if (off(px, py)) { acc[1].x += px; acc[1].y += py; acc[1].n++; }
            }
        for (const Raider& r : w.raiders)
            if (r.alive) {
                float px = r.x * TILE_PX + 2.f, py = r.y * TILE_PX + 2.f;
                if (off(px, py)) { acc[2].x += px; acc[2].y += py; acc[2].n++; }
            }
        float cx = g.viewW / 2.f, cy = g.viewH / 2.f;
        for (auto& a : acc) {
            if (!a.n) continue;
            float sx = (a.x / a.n - g.camX) * g.zoom, sy = (a.y / a.n - g.camY) * g.zoom;
            float dx = sx - cx, dy = sy - cy, len = std::hypot(dx, dy);
            if (len < 1.f) continue;
            dx /= len; dy /= len;
            float m = 9.f, ex = cx - m - g.safeTop * 0.f, ey = cy - m;
            float t = std::min(std::abs(dx) > 1e-4f ? (cx - m) / std::abs(dx) : 1e9f,
                               std::abs(dy) > 1e-4f ? (cy - m - (dy < 0 ? g.safeTop : 0)) / std::abs(dy) : 1e9f);
            (void)ex; (void)ey;
            float ax = cx + dx * t, ay = cy + dy * t;
            // filled triangle pointing outwards
            SDL_FColor c{((a.col >> 16) & 255) / 255.f, ((a.col >> 8) & 255) / 255.f, (a.col & 255) / 255.f, 1.f};
            SDL_FColor k{0.08f, 0.06f, 0.09f, 1.f};
            float px = -dy, py = dx;
            auto tri = [&](float s, SDL_FColor col) {
                SDL_Vertex vt[3] = {
                    {{ax + dx * 6 * s, ay + dy * 6 * s}, col, {0, 0}},
                    {{ax - dx * 4 * s + px * 5 * s, ay - dy * 4 * s + py * 5 * s}, col, {0, 0}},
                    {{ax - dx * 4 * s - px * 5 * s, ay - dy * 4 * s - py * 5 * s}, col, {0, 0}}};
                SDL_RenderGeometry(ren, nullptr, vt, 3, nullptr, 0);
            };
            tri(1.35f, k);
            tri(1.f, c);
        }
    }

    // recent events (top-left), fading after ~2 days
    size_t maxChars = (size_t)std::max(8, (g.scrW - 8) / 8);
    int shown = 0;
    for (int i = (int)w.log.size() - 1; i >= 0 && shown < 4; i--) {
        const LogLine& l = w.log[i];
        if (w.day() + 1 - l.day > 2) break;
        std::string s = "D" + std::to_string(l.day) + " " + l.text;
        if (s.size() > maxChars) s = s.substr(0, maxChars);
        float y = 3.f + g.safeTop + shown * 10.f;
        rect(ren, 1, y - 1, 8.f * s.size() + 4, 10, 0x000000, 150);
        bool alarm = l.text.find('!') != std::string::npos;
        text(ren, 3, y, alarm ? 0xf9c22b : 0xc7dcd0, s);
        shown++;
    }

    if (w.gameOver()) {
        char buf[96];
        float bw = std::min(300.f, g.scrW - 10.f), bh = 64, bx = (g.scrW - bw) / 2, by = (g.viewH - bh) / 2;
        rect(ren, bx, by, bw, bh, 0x2e222f, 235);
        float tx = bx + 6;
        text(ren, tx, by + 8, 0xe83b3b, "YOUR CIVILIZATION FELL");
        std::snprintf(buf, sizeof(buf), "Survived %d days", w.day());
        text(ren, tx, by + 22, 0xffffff, buf);
        std::snprintf(buf, sizeof(buf), "Best %d  Born %d  Died %d", g.best, w.births, w.deaths);
        text(ren, tx, by + 34, 0x9babb2, buf);
        text(ren, tx, by + 50, 0xf9c22b, "R or tap: new world");
    }
}

// Pick a logical resolution for the current window: whole-number pixel scale with the
// short side around 270 px, so phones in either orientation and desktops all look the same size.
static void relayout(SDL_Renderer* ren, Game& g) {
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    int scale = std::max(1, std::min(pw, ph) / 270);
    g.scrW = std::max(160, pw / scale);
    g.scrH = std::max(160, ph / scale);
    SDL_SetRenderLogicalPresentation(ren, g.scrW, g.scrH, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    {   // safe area (notches, rounded corners, home indicator) from window points to logical px
        SDL_Window* win = SDL_GetRenderWindow(ren);
        int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh);
        SDL_Rect sa{0, 0, ww, wh};
        g.safeTop = g.safeBottom = 0;
        if (wh > 0 && SDL_GetWindowSafeArea(win, &sa)) {
            float k = (float)g.scrH / wh;
            g.safeTop = (int)std::ceil(sa.y * k);
            g.safeBottom = (int)std::ceil((wh - sa.y - sa.h) * k);
        }
    }
    g.hudH = 0;
    g.hudH = layoutHud(ren, g, false);
    g.hudH = layoutHud(ren, g, false);   // second pass with the real top
    g.viewH = g.scrH - g.hudH;
    g.viewW = g.scrW;
    g.clampCam();
}

// Touch: one finger tap = select button / cast, one finger drag = pan, two fingers = pinch zoom.
struct TouchState {
    SDL_FingerID id[2]{}; float x[2]{}, y[2]{};   // logical px
    int n = 0;
    float downX = 0, downY = 0; bool moved = false;
    float pinchStart = 0; int zoomStart = 0;
    uint64_t downTime = 0; bool aiming = false;
    static constexpr float AIM_LIFT = 28.f;   // ring floats this far above the finger
};
static TouchState touch;

static void fingerToLogical(SDL_Renderer* ren, const SDL_TouchFingerEvent& f, float& lx, float& ly) {
    int ww = 0, wh = 0; SDL_GetWindowSize(SDL_GetRenderWindow(ren), &ww, &wh);
    SDL_RenderCoordinatesFromWindow(ren, f.x * ww, f.y * wh, &lx, &ly);
}

static void tapAt(Game& g, float lx, float ly) {
    SDL_FPoint pt{lx, ly};
    for (int i = 0; i < (int)Power::Count; i++)
        if (SDL_PointInRectFloat(&pt, &g.powerBtn[i])) { g.power = i; return; }
    if (SDL_PointInRectFloat(&pt, &g.speedBtn)) { g.cycleSpeed(); return; }
    if (g.world->gameOver()) { g.restart(g.seed + 1); return; }
    if (ly >= 0 && ly < g.viewH) {
        float wx, wy; g.toWorld(lx, ly, wx, wy);
        g.world->cast((Power)g.power, (int)(wx / TILE_PX), (int)(wy / TILE_PX));
    }
}

static void handleTouch(SDL_Renderer* ren, Game& g, const SDL_Event& e) {
    const SDL_TouchFingerEvent& f = e.tfinger;
    float lx, ly; fingerToLogical(ren, f, lx, ly);
    int slot = -1;
    for (int i = 0; i < touch.n; i++) if (touch.id[i] == f.fingerID) slot = i;
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (touch.n >= 2) return;
        slot = touch.n++;
        touch.id[slot] = f.fingerID; touch.x[slot] = lx; touch.y[slot] = ly;
        if (touch.n == 1) { touch.downX = lx; touch.downY = ly; touch.moved = false; touch.aiming = false; touch.downTime = SDL_GetTicks(); }
        else {
            touch.moved = true;   // a pinch is never a tap
            touch.aiming = false;
            touch.pinchStart = std::hypot(touch.x[1] - touch.x[0], touch.y[1] - touch.y[0]);
            touch.zoomStart = g.zoom;
        }
    } else if (e.type == SDL_EVENT_FINGER_MOTION && slot >= 0) {
        float dx = lx - touch.x[slot], dy = ly - touch.y[slot];
        touch.x[slot] = lx; touch.y[slot] = ly;
        if (touch.n == 1) {
            if (touch.aiming) return;   // ring follows the finger; no panning while aiming
            if (std::hypot(lx - touch.downX, ly - touch.downY) > 6.f) touch.moved = true;
            if (touch.moved) { g.camX -= dx / g.zoom; g.camY -= dy / g.zoom; g.clampCam(); }
        } else if (touch.pinchStart > 1.f) {
            float d = std::hypot(touch.x[1] - touch.x[0], touch.y[1] - touch.y[0]);
            int z = touch.zoomStart + (int)std::lround(std::log2(d / touch.pinchStart) * 2.f);
            if (z != g.zoom) g.setZoom(z, (touch.x[0] + touch.x[1]) / 2, (touch.y[0] + touch.y[1]) / 2);
        }
    } else if (e.type == SDL_EVENT_FINGER_UP && slot >= 0) {
        if (touch.n == 1 && touch.aiming) {
            float ay = ly - TouchState::AIM_LIFT;
            if (ay >= 0 && ay < g.viewH) {
                float wx, wy; g.toWorld(lx, ay, wx, wy);
                g.world->cast((Power)g.power, (int)(wx / TILE_PX), (int)(wy / TILE_PX));
            }
            touch.aiming = false;
        } else if (touch.n == 1 && !touch.moved) tapAt(g, lx, ly);
        touch.id[slot] = touch.id[touch.n - 1]; touch.x[slot] = touch.x[touch.n - 1]; touch.y[slot] = touch.y[touch.n - 1];
        touch.n--;
        if (touch.n == 1) { touch.downX = touch.x[0]; touch.downY = touch.y[0]; }
    }
}

int main(int argc, char** argv) {
    const char* shotPath = nullptr;
    int shotDays = 0;
    int winW = 324, winH = 702;   // portrait phone shape (iPhone-ish 9:19.5) on desktop
    uint32_t seed = (uint32_t)std::time(nullptr);
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--days") && i + 1 < argc) shotDays = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("villagesim [--seed N] [--size WxH]\n  --shot out.bmp [--days N]  render one frame after N days and exit\n");
            return 0;
        }
    }
    if (shotPath) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");   // vertical-only game
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr;
    if (!SDL_CreateWindowAndRenderer("A2D Village Sim", winW, winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    Game g;
    g.best = loadBest();
    g.restart(seed);
    relayout(ren, g);
    g.zoom = std::max(g.minZoom(), 2);
    g.centerOnHall();
    if (shotPath) {
        g.power = 0;
        for (int t = 0; t < shotDays * TICKS_PER_DAY + TICKS_PER_DAY / 2 && !g.world->gameOver(); t++) g.world->tick();
        g.centerOnHall();
    }

    uint64_t last = SDL_GetTicks();
    double acc = 0.0;
    int frame = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) relayout(ren, g);
            else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_BACKGROUND) g.paused = true;
            else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP)
                handleTouch(ren, g, e);
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k >= SDLK_1 && k <= SDLK_4) g.power = (int)(k - SDLK_1);
                else if (k == SDLK_SPACE) g.paused = !g.paused;
                else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) g.speed = std::min(8, g.speed * 2);
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) g.speed = std::max(1, g.speed / 2);
                else if (k == SDLK_H) g.centerOnHall();
                else if (k == SDLK_R && (g.world->gameOver() || (e.key.mod & SDL_KMOD_SHIFT))) g.restart(g.seed + 1);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                float lx, ly;
                SDL_RenderCoordinatesFromWindow(ren, e.button.x, e.button.y, &lx, &ly);
                SDL_FPoint pt{lx, ly};
                bool hit = false;
                for (int i = 0; i < (int)Power::Count; i++)
                    if (SDL_PointInRectFloat(&pt, &g.powerBtn[i])) { g.power = i; hit = true; }
                if (!hit && SDL_PointInRectFloat(&pt, &g.speedBtn)) { g.cycleSpeed(); hit = true; }
                if (!hit && g.world->gameOver()) { g.restart(g.seed + 1); hit = true; }
                if (!hit && ly >= 0 && ly < g.viewH) {
                    float wx, wy; g.toWorld(lx, ly, wx, wy);
                    g.world->cast((Power)g.power, (int)(wx / TILE_PX), (int)(wy / TILE_PX));
                }
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                float lx, ly;
                SDL_RenderCoordinatesFromWindow(ren, e.wheel.mouse_x, e.wheel.mouse_y, &lx, &ly);
                if (e.wheel.y > 0) g.setZoom(g.zoom + 1, lx, ly);
                else if (e.wheel.y < 0) g.setZoom(g.zoom - 1, lx, ly);
            } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & (SDL_BUTTON_RMASK | SDL_BUTTON_MMASK))) {
                // drag to pan: convert window delta to logical delta
                float ax, ay, bx, by;
                SDL_RenderCoordinatesFromWindow(ren, 0, 0, &ax, &ay);
                SDL_RenderCoordinatesFromWindow(ren, e.motion.xrel, e.motion.yrel, &bx, &by);
                g.camX -= (bx - ax) / g.zoom; g.camY -= (by - ay) / g.zoom; g.clampCam();
            }
        }
        uint64_t now = SDL_GetTicks();
        {   // keyboard pan
            const bool* ks = SDL_GetKeyboardState(nullptr);
            float pan = (now - last) * 0.25f / g.zoom;
            if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]) g.camX -= pan;
            if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) g.camX += pan;
            if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]) g.camY -= pan;
            if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) g.camY += pan;
            g.clampCam();
        }
        acc += (now - last) / 1000.0;
        last = now;
        if (acc > 0.5) acc = 0.5;
        while (acc >= 0.1) {
            acc -= 0.1;
            if (!g.paused) for (int s = 0; s < g.speed; s++) g.world->tick();
        }
        if (g.world->gameOver() && !g.bestSaved) {
            if (g.world->day() > g.best) { g.best = g.world->day(); saveBest(g.best); }
            g.bestSaved = true;
        }

        float wx, wy, lx = -1, ly = -1;
        SDL_GetMouseState(&wx, &wy);
        SDL_RenderCoordinatesFromWindow(ren, wx, wy, &lx, &ly);
        float cwx = -100, cwy = -100;
        if (ly >= 0 && ly < g.viewH) g.toWorld(lx, ly, cwx, cwy);
        // touch: hold still on the map for 180 ms to aim; the ring sits above the finger
        if (touch.n == 1 && !touch.moved && !touch.aiming && touch.downY < g.viewH && now - touch.downTime > 180)
            touch.aiming = true;
        if (touch.aiming) {
            cwx = cwy = -100;
            float ay = touch.y[0] - TouchState::AIM_LIFT;
            if (ay >= 0 && ay < g.viewH) g.toWorld(touch.x[0], ay, cwx, cwy);
        } else if (touch.n > 0) cwx = cwy = -100;
        if (shotPath) { cwx = g.world->hallX * TILE_PX + 30.f; cwy = g.world->hallY * TILE_PX + 8.f; }

        void* pixels; int pitch;
        if (SDL_LockTexture(tex, nullptr, &pixels, &pitch)) {
            drawWorld(*g.world, (uint32_t*)pixels, pitch / 4, frame, (int)cwx, (int)cwy, g.power);
            SDL_UnlockTexture(tex);
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (g.layoutDirty()) relayout(ren, g);
        SDL_FRect dst{0, 0, (float)g.viewW, (float)g.viewH};
        SDL_FRect src{g.camX, g.camY, (float)g.viewW / g.zoom, (float)g.viewH / g.zoom};
        SDL_RenderTexture(ren, tex, &src, &dst);
        drawHud(ren, g);
        if (shotPath) {
            SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr);
            if (s) { SDL_SaveBMP(s, shotPath); SDL_DestroySurface(s); std::printf("wrote %s\n", shotPath); }
            running = false;
        }
        SDL_RenderPresent(ren);
        frame++;
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
