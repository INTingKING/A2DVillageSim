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
    int zoom = 2;           // 1..4, world pixels are drawn zoom x zoom
    float camX = 0, camY = 0; // top-left of the view in world pixels
    void centerOnHall() {
        camX = world->hallX * TILE_PX + 2 - VIEW_W / (2.f * zoom);
        camY = world->hallY * TILE_PX + 2 - VIEW_H / (2.f * zoom);
        clampCam();
    }
    void clampCam() {
        float vw = (float)VIEW_W / zoom, vh = (float)VIEW_H / zoom;
        camX = std::clamp(camX, 0.f, VIEW_W - vw);
        camY = std::clamp(camY, 0.f, VIEW_H - vh);
    }
    // logical screen -> world pixel
    void toWorld(float lx, float ly, float& wx, float& wy) const { wx = camX + lx / zoom; wy = camY + ly / zoom; }
    void setZoom(int z, float lx, float ly) {
        float wx, wy; toWorld(lx, ly, wx, wy);
        zoom = std::clamp(z, 1, 4);
        camX = wx - lx / zoom; camY = wy - ly / zoom;
        clampCam();
    }
    void restart(uint32_t s) { seed = s; world = std::make_unique<World>(s, true); bestSaved = false; centerOnHall(); }
};

static void text(SDL_Renderer* r, float x, float y, uint32_t rgb, const std::string& s) {
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
}
static void rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t rgb, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
    SDL_FRect fr{x, y, w, h};
    SDL_RenderFillRect(r, &fr);
}

static void drawHud(SDL_Renderer* ren, const Game& g, float mx, float my) {
    const World& w = *g.world;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // bottom bar
    rect(ren, 0, VIEW_H, SCREEN_W, HUD_H, 0x2e222f);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Day %d %s  Pop %d  Food %d  Wood %d",
                  w.day() + 1, seasonName(w.season()), w.population(), (int)w.food, (int)w.wood);
    std::string stats = buf;
    if (w.sickCount() > 0) stats += "  Sick " + std::to_string(w.sickCount());
    text(ren, 3, VIEW_H + 3, 0xc7dcd0, stats);
    // mana: a thin bar along the top edge of the HUD, plus the number on the right
    rect(ren, 0, VIEW_H, SCREEN_W, 2, 0x323353);
    rect(ren, 0, VIEW_H, SCREEN_W * w.mana / 100.f, 2, 0x905ea9);
    std::snprintf(buf, sizeof(buf), "Mana %d", (int)w.mana);
    text(ren, SCREEN_W - 8.f * std::strlen(buf) - 3, VIEW_H + 3, 0xa884f3, buf);
    // powers
    float x = 3;
    for (int i = 0; i < (int)Power::Count; i++) {
        const PowerInfo& pi = powerInfo((Power)i);
        std::snprintf(buf, sizeof(buf), "%d %s %d", i + 1, pi.name, pi.cost);
        float wpx = (float)std::strlen(buf) * 8 + 6;
        bool sel = g.power == i;
        bool afford = w.mana >= pi.cost;
        if (sel) rect(ren, x - 2, VIEW_H + 13, wpx, 10, 0x6b3e75);
        text(ren, x, VIEW_H + 14, afford ? (sel ? 0xffffff : 0x9babb2) : 0x7a3045, buf);
        x += wpx + 4;
    }
    std::snprintf(buf, sizeof(buf), "%s x%d", g.paused ? "PAUSED" : "", g.speed);
    text(ren, SCREEN_W - 8.f * std::strlen(buf) - 3, VIEW_H + 14, g.paused ? 0xf9c22b : 0x7f708a, buf);

    // recent events (top-left), fading after ~2 days
    int shown = 0;
    for (int i = (int)w.log.size() - 1; i >= 0 && shown < 4; i--) {
        const LogLine& l = w.log[i];
        if (w.day() + 1 - l.day > 2) break;
        std::string s = "D" + std::to_string(l.day) + " " + l.text;
        if (s.size() > 58) s = s.substr(0, 58);
        float y = 3.f + shown * 10.f;
        rect(ren, 1, y - 1, 8.f * s.size() + 4, 10, 0x000000, 150);
        bool alarm = l.text.find('!') != std::string::npos;
        text(ren, 3, y, alarm ? 0xf9c22b : 0xc7dcd0, s);
        shown++;
    }

    // tooltip for selected power near the cursor
    if (my >= 0 && my < VIEW_H && mx >= 0) {
        const PowerInfo& pi = powerInfo((Power)g.power);
        (void)pi;
    }

    if (w.gameOver()) {
        float bw = 300, bh = 64, bx = (SCREEN_W - bw) / 2, by = (VIEW_H - bh) / 2;
        rect(ren, bx, by, bw, bh, 0x2e222f, 235);
        text(ren, bx + 10, by + 8, 0xe83b3b, "YOUR CIVILIZATION HAS FALLEN");
        std::snprintf(buf, sizeof(buf), "Survived %d days   Best %d", w.day(), g.best);
        text(ren, bx + 10, by + 24, 0xffffff, buf);
        std::snprintf(buf, sizeof(buf), "Born %d   Died %d", w.births, w.deaths);
        text(ren, bx + 10, by + 36, 0x9babb2, buf);
        text(ren, bx + 10, by + 50, 0xf9c22b, "Press R for a new world");
    }
}

int main(int argc, char** argv) {
    const char* shotPath = nullptr;
    int shotDays = 0;
    uint32_t seed = (uint32_t)std::time(nullptr);
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--days") && i + 1 < argc) shotDays = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("villagesim [--seed N]\n  --shot out.bmp [--days N]  render one frame after N days and exit\n");
            return 0;
        }
    }
    if (shotPath) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr;
    if (!SDL_CreateWindowAndRenderer("A2D Village Sim", SCREEN_W * 3, SCREEN_H * 3, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(ren, 1);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    Game g;
    g.best = loadBest();
    g.restart(seed);
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
                if (ly >= 0 && ly < VIEW_H) {
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
        if (ly >= 0 && ly < VIEW_H) g.toWorld(lx, ly, cwx, cwy);
        if (shotPath) { cwx = g.world->hallX * TILE_PX + 30.f; cwy = g.world->hallY * TILE_PX + 8.f; }

        void* pixels; int pitch;
        if (SDL_LockTexture(tex, nullptr, &pixels, &pitch)) {
            drawWorld(*g.world, (uint32_t*)pixels, pitch / 4, frame, (int)cwx, (int)cwy, g.power);
            SDL_UnlockTexture(tex);
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_FRect dst{0, 0, (float)VIEW_W, (float)VIEW_H};
        SDL_FRect src{g.camX, g.camY, (float)VIEW_W / g.zoom, (float)VIEW_H / g.zoom};
        SDL_RenderTexture(ren, tex, &src, &dst);
        drawHud(ren, g, lx, ly);
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
