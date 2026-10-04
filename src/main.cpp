// A2DVillageSim: an Anno-style survival town builder for portrait phones.
// Touch: build drawer at the bottom, tap a card, drag the ghost, Build/Cancel.
// Road tool: drag to paint. Two fingers pan and pinch-zoom.
// Desktop: left click = tap (click places directly), right-drag pan, wheel zoom,
// Esc cancel tool, Space pause, +/- speed, H back to the hall, Shift+R restart.
#include <SDL3/SDL.h>
#include "autoplay.hpp"
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

// Daily run: everyone gets the same world (and so the same disaster dice) on the same date.
static uint32_t todaySeed() {
    std::time_t t = std::time(nullptr); std::tm lt = *std::localtime(&t);
    return (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
}
static std::string bestFile(bool daily) { return daily ? "best_daily_" + std::to_string(todaySeed()) + ".txt" : "best_score.txt"; }
static int loadBest(bool daily = false) { std::ifstream f(bestFile(daily)); int b = 0; if (f) f >> b; return b; }
static void saveBest(int b, bool daily = false) { std::ofstream f(bestFile(daily)); f << b; }

static uint32_t eventColor(Event e) {
    switch (e) {
    case Event::Raiders: return 0xe83b3b;
    case Event::Wildfire: case Event::Drought: return 0xfb6b1d;
    case Event::Plague: case Event::Locusts: return 0x9cdb43;
    case Event::Blizzard: return 0xc7dcd0;
    default: return 0xffffff;
    }
}
// Word-wraps s into lines of at most n characters.
static std::vector<std::string> wrap(const std::string& s, size_t n) {
    std::vector<std::string> out; std::string line, word;
    auto flush = [&]() { if (!word.empty()) { if (!line.empty() && line.size() + 1 + word.size() > n) { out.push_back(line); line.clear(); } line += (line.empty() ? "" : " ") + word; word.clear(); } };
    for (char c : s) { if (c == ' ') flush(); else word += c; }
    flush(); if (!line.empty()) out.push_back(line);
    return out;
}

// UI palette (Designer)
constexpr uint32_t UI_BG = 0x2e222f, UI_PANEL = 0x3e3546, UI_SEL = 0x6b3e75, UI_TEXT = 0xffffff, UI_DIM = 0x9babb2,
                   UI_BAD = 0xe83b3b, UI_GOOD = 0x1ebc73, UI_WARN = 0xf9c22b, UI_ACCENT = 0xa884f3;
static const uint32_t TAB_COL[4] = {0xb33831, 0xe0a83a, 0x5b6b2e, 0x5a6e9c};   // roof colours per category

enum class Tool { None, Place, Road, Remove };
enum BtnId { B_TAB0 = 0, B_ROAD = 10, B_REMOVE, B_CARD0 = 20, B_OK = 40, B_CANCEL, B_SPEED, B_RESTART, B_ZIN, B_ZOUT, B_DAILY };
struct Btn { SDL_FRect r; int id; };

static const char* shortName(BType t) {
    switch (t) {
    case BType::House: return "House"; case BType::Lumber: return "Lumber"; case BType::Sawmill: return "Sawmill";
    case BType::Fisher: return "Fisher"; case BType::Farm: return "Farm"; case BType::Mill: return "Mill";
    case BType::Bakery: return "Bakery"; case BType::Well: return "Well"; case BType::Healer: return "Healer";
    case BType::Tower: return "Tower"; default: return "Hall";
    }
}
static const char* tabName(int c) { static const char* n[4] = {"Home", "Food", "Wood", "Guard"}; return n[c]; }

struct Game {
    std::unique_ptr<World> world;
    int speed = 1; bool paused = false;
    int best = 0, bestDaily = 0; bool bestSaved = false; uint32_t seed = 1; bool daily = false;
    Event warnedFor = Event::None;
    float zoom = 2; float camX = 0, camY = 0;
    int scrW = 270, scrH = 585;
    int viewY = 0, viewW = 270, viewH = 400;   // map viewport between the top bar and the drawer
    int safeTop = 0, safeBottom = 0;
    // tools
    Tool tool = Tool::None; int tab = -1; BType placing = BType::House;
    int gx = 0, gy = 0; bool ghostSet = false;
    std::vector<int> roadDrag; bool painting = false;
    int remX = -1, remY = -1; int selected = -1;
    std::string toast; uint64_t toastUntil = 0;
    std::vector<Btn> btns;

    // Zoom steps (below 1 = zoomed out). Each step is snapped so one world pixel covers a whole
    // number of screen pixels at the current UI scale, which keeps pixel art even when panning.
    std::vector<float> ZL{0.5f, 1.f, 2.f, 3.f, 4.f};
    void buildZoomSteps(int uiScale) {
        ZL.clear();
        for (float z : {0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f}) {
            float snapped = std::max(1.f, std::round(z * uiScale)) / uiScale;
            if (ZL.empty() || snapped > ZL.back() + 1e-4f) ZL.push_back(snapped);
        }
    }
    float minZoom() const {   // lowest step that still fills the viewport with map
        float fit = std::max((float)viewW / VIEW_W, (float)viewH / VIEW_H);
        for (float z : ZL) if (z >= fit) return z;
        return ZL.back();
    }
    float maxZoom() const { return ZL.back(); }
    int zoomIndex() const { int best = 0; for (int i = 1; i < (int)ZL.size(); i++) if (std::abs(ZL[i] - zoom) < std::abs(ZL[best] - zoom)) best = i; return best; }
    void stepZoom(int d, float lx, float ly) { setZoom(ZL[std::clamp(zoomIndex() + d, 0, (int)ZL.size() - 1)], lx, ly); }
    void stepZoomCenter(int d) { stepZoom(d, viewW / 2.f, viewY + viewH / 2.f); }
    void clampCam() {
        zoom = std::clamp(zoom, minZoom(), maxZoom());
        float vw = (float)viewW / zoom, vh = (float)viewH / zoom;
        camX = std::clamp(camX, 0.f, std::max(0.f, VIEW_W - vw));
        camY = std::clamp(camY, 0.f, std::max(0.f, VIEW_H - vh));
    }
    void centerOn(float wx, float wy) { camX = wx - viewW / (2.f * zoom); camY = wy - viewH / (2.f * zoom); clampCam(); }
    void centerOnHall() { centerOn((world->hallX + 1.5f) * TILE_PX, (world->hallY + 1.5f) * TILE_PX); }
    bool inView(float lx, float ly) const { return ly >= viewY && ly < viewY + viewH && lx >= 0 && lx < viewW; }
    void toWorld(float lx, float ly, float& wx, float& wy) const { wx = camX + lx / zoom; wy = camY + (ly - viewY) / zoom; }
    void toTile(float lx, float ly, int& tx, int& ty) const { float wx, wy; toWorld(lx, ly, wx, wy); tx = (int)std::floor(wx / TILE_PX); ty = (int)std::floor(wy / TILE_PX); }
    void setZoom(float z, float lx, float ly) {
        float wx, wy; toWorld(lx, ly, wx, wy);
        zoom = std::clamp(z, minZoom(), maxZoom());
        camX = wx - lx / zoom; camY = wy - (ly - viewY) / zoom; clampCam();
    }
    void cycleSpeed() { if (paused) { paused = false; speed = 1; } else if (speed >= 8) paused = true; else speed *= 2; }
    void restart(uint32_t s, bool isDaily = false) { seed = s; daily = isDaily; warnedFor = Event::None; world = std::make_unique<World>(s, true); bestSaved = false; cancelTool(); tab = -1; centerOnHall(); }
    void say(const std::string& s) { toast = s; toastUntil = SDL_GetTicks() + 2500; }
    void cancelTool() { tool = Tool::None; ghostSet = false; roadDrag.clear(); painting = false; remX = remY = -1; }
    void startPlace(BType t) {
        tool = Tool::Place; placing = t; selected = -1;
        // ghost starts at the view centre
        int tx, ty; toTile(viewW / 2.f, viewY + viewH / 2.f, tx, ty);
        const BInfo& in = binfo(t);
        gx = tx - in.w / 2; gy = ty - in.h / 2; ghostSet = true;
    }
    void moveGhost(float lx, float ly) {
        int tx, ty; toTile(lx, ly, tx, ty);
        const BInfo& in = binfo(placing);
        gx = std::clamp(tx - in.w / 2, 0, MAP_W - in.w); gy = std::clamp(ty - in.h / 2, 0, MAP_H - in.h);
    }
    bool confirmPlace() {
        World& w = *world;
        if (!w.canAfford(placing)) { say("Not enough materials"); return false; }
        if (!w.canPlace(placing, gx, gy)) { say(placing == BType::Fisher ? "Needs water nearby" : "Can't build there"); return false; }
        w.place(placing, gx, gy);
        int bi = w.buildingAt(gx, gy);
        if (bi >= 0 && !w.buildings[bi].connected) say("Connect it to the hall with a road");
        else say(std::string(shortName(placing)) + " built");
        if (!w.canAfford(placing)) cancelTool();   // keep placing more while affordable
        return true;
    }
    void addRoadTile(int tx, int ty) {
        if (!world->inside(tx, ty)) return;
        int i = ty * MAP_W + tx;
        if (!roadDrag.empty()) {   // fill gaps with 4-neighbour steps so roads stay connected
            int last = roadDrag.back(); if (last == i) return;
            int lx = last % MAP_W, ly = last / MAP_W;
            while (lx != tx || ly != ty) {
                if (std::abs(tx - lx) >= std::abs(ty - ly)) lx += tx > lx ? 1 : -1; else ly += ty > ly ? 1 : -1;
                int j = ly * MAP_W + lx;
                if (std::find(roadDrag.begin(), roadDrag.end(), j) == roadDrag.end()) roadDrag.push_back(j);
            }
        } else roadDrag.push_back(i);
    }
    int roadCost() const { int n = 0; for (int i : roadDrag) n += world->canRoad(i % MAP_W, i / MAP_W); return n; }
    void commitRoad() {
        int built = 0, wanted = roadCost();
        for (int i : roadDrag) built += world->placeRoad(i % MAP_W, i / MAP_W);
        if (built < wanted) say("Out of logs: built " + std::to_string(built) + " of " + std::to_string(wanted));
        roadDrag.clear(); painting = false;
    }
};

// 1px dark outline so text reads in sunlight over any terrain.
static void text(SDL_Renderer* r, float x, float y, uint32_t rgb, const std::string& s, float scale = 1.f) {
    float sx, sy; SDL_GetRenderScale(r, &sx, &sy);
    if (scale != 1.f) { SDL_SetRenderScale(r, sx * scale, sy * scale); x /= scale; y /= scale; }
    SDL_SetRenderDrawColor(r, 0x14, 0x10, 0x18, 255);
    const float o[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& d : o) SDL_RenderDebugText(r, x + d[0] / scale, y + d[1] / scale, s.c_str());
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
    if (scale != 1.f) SDL_SetRenderScale(r, sx, sy);
}
static void rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t rgb, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
    SDL_FRect fr{x, y, w, h};
    SDL_RenderFillRect(r, &fr);
}
static void button(SDL_Renderer* r, const SDL_FRect& b, uint32_t bg, bool sel, uint32_t accent) {
    rect(r, b.x, b.y, b.w, b.h, bg);
    rect(r, b.x, b.y, b.w, 1, 0x5d4b62);             // top-left light
    rect(r, b.x, b.y + b.h - 2, b.w, 2, sel ? accent : 0x1d161f);
}
static void centered(SDL_Renderer* r, const SDL_FRect& b, float y, uint32_t col, const std::string& s) {
    text(r, b.x + (b.w - 8.f * s.size()) / 2.f, y, col, s);
}

static std::string costStr(BType t) {
    const BInfo& in = binfo(t);
    std::string s;
    if (in.costLogs) s += std::to_string(in.costLogs) + "L";
    if (in.costPlanks) s += (s.empty() ? "" : " ") + std::to_string(in.costPlanks) + "P";
    return s.empty() ? "free" : s;
}

static std::vector<BType> tabItems(int c) {
    std::vector<BType> v;
    for (int i = 1; i < (int)BType::Count; i++) if ((int)binfo((BType)i).cat == c) v.push_back((BType)i);
    return v;
}

// Lays out and draws all UI; fills g.btns and the map viewport. draw=false only measures.
static void ui(SDL_Renderer* ren, Game& g, bool draw) {
    World& w = *g.world;
    g.btns.clear();
    const float pad = 4.f, btnH = 28.f;
    // ---- top bar: big day counter, speed, resources
    float y = (float)g.safeTop + pad;
    float topStart = 0;
    std::string dayS = "DAY " + std::to_string(w.day() + 1);
    std::vector<std::pair<std::string, uint32_t>> stats = {
        {"Pop " + std::to_string(w.population()) + "/" + std::to_string(w.housing()), UI_TEXT},
        {"Bread " + std::to_string((int)w.store[(int)Res::Bread]), w.food() < w.population() * 2 ? UI_WARN : UI_TEXT},
        {"Fish " + std::to_string((int)w.store[(int)Res::Fish]), UI_TEXT},
        {"Logs " + std::to_string((int)w.store[(int)Res::Logs]), 0xc09473},
        {"Planks " + std::to_string((int)w.store[(int)Res::Planks]), 0xe6c89a},
        {"Wheat " + std::to_string((int)w.store[(int)Res::Wheat]), UI_DIM},
        {"Flour " + std::to_string((int)w.store[(int)Res::Flour]), UI_DIM}};
    if (w.jobsOpen() > 0) stats.push_back({"Jobs open " + std::to_string(w.jobsOpen()), UI_WARN});
    if (w.sickCount() > 0) stats.push_back({"Sick " + std::to_string(w.sickCount()), 0x9cdb43});
    // measure rows
    float sx = pad, sy = y + 22.f; int rows = 1;
    for (auto& s : stats) { float tw = 8.f * s.first.size(); if (sx > pad && sx + tw > g.scrW - pad) { sx = pad; sy += 11.f; rows++; } sx += tw + 10.f; }
    float topH = g.safeTop + pad + 22.f + rows * 11.f + 3.f;
    if (draw) {
        rect(ren, 0, topStart, (float)g.scrW, topH, UI_BG, 235);
        rect(ren, 0, topH - 1, (float)g.scrW, 1, 0x1d161f);
        text(ren, pad, y + 1, UI_TEXT, dayS, 2.f);
        text(ren, pad + 16.f * dayS.size() + 6.f, y + 5, UI_DIM, seasonName(w.season()));
        if (g.daily) text(ren, pad + 16.f * dayS.size() + 6.f + 8.f * (std::strlen(seasonName(w.season())) + 1), y + 5, UI_WARN, "DAILY");
        sx = pad; sy = y + 22.f;
        for (auto& s : stats) { float tw = 8.f * s.first.size(); if (sx > pad && sx + tw > g.scrW - pad) { sx = pad; sy += 11.f; } text(ren, sx, sy, s.second, s.first); sx += tw + 10.f; }
    }
    {
        std::string sp = g.paused ? "PAUSE" : "x" + std::to_string(g.speed);
        SDL_FRect b{g.scrW - pad - 52.f, y - 2.f, 52.f, 20.f};
        g.btns.push_back({b, B_SPEED});
        if (draw) { button(ren, b, g.paused ? UI_SEL : UI_PANEL, g.paused, UI_WARN); centered(ren, b, b.y + 6, g.paused ? UI_WARN : UI_TEXT, sp); }
    }

    // ---- bottom drawer (built from the bottom up)
    float bottom = (float)g.scrH - g.safeBottom;
    float tabsY = bottom - btnH - pad;
    std::vector<BType> cards = g.tab >= 0 ? tabItems(g.tab) : std::vector<BType>{};
    float cardH = 46.f;
    float cardsY = tabsY - (cards.empty() ? 0.f : cardH + pad);
    bool confirmRow = g.tool != Tool::None || g.selected >= 0;
    float confY = cardsY - (confirmRow ? btnH + pad : 0.f);
    float drawerTop = confY - pad;
    if (draw) {
        rect(ren, 0, drawerTop, (float)g.scrW, (float)g.scrH - drawerTop, UI_BG, 235);
        rect(ren, 0, drawerTop, (float)g.scrW, 1, 0x5d4b62);
    }
    // tabs: 4 categories + Road + Remove, equal widths, thumb height
    {
        int n = 6; float tw = (g.scrW - pad * (n + 1)) / n;
        for (int i = 0; i < n; i++) {
            SDL_FRect b{pad + i * (tw + pad), tabsY, tw, btnH};
            int id = i < 4 ? B_TAB0 + i : (i == 4 ? B_ROAD : B_REMOVE);
            g.btns.push_back({b, id});
            if (!draw) continue;
            bool sel = (i < 4 && g.tab == i) || (i == 4 && g.tool == Tool::Road) || (i == 5 && g.tool == Tool::Remove);
            uint32_t acc = i < 4 ? TAB_COL[i] : (i == 4 ? 0xa77b5b : UI_BAD);
            button(ren, b, sel ? UI_SEL : UI_PANEL, sel, acc);
            rect(ren, b.x + 3, b.y + 3, 4, 4, acc);   // colour key matching the roofs
            centered(ren, b, b.y + 11, sel ? UI_TEXT : UI_DIM, i < 4 ? tabName(i) : (i == 4 ? "Road" : "Del"));
        }
    }
    // building cards
    if (!cards.empty()) {
        int n = (int)cards.size(); float cw = std::min(90.f, (g.scrW - pad * (n + 1)) / n);
        for (int i = 0; i < n; i++) {
            SDL_FRect b{pad + i * (cw + pad), cardsY, cw, cardH};
            g.btns.push_back({b, B_CARD0 + (int)cards[i]});
            if (!draw) continue;
            bool afford = w.canAfford(cards[i]);
            bool sel = g.tool == Tool::Place && g.placing == cards[i];
            button(ren, b, sel ? UI_SEL : UI_PANEL, sel, TAB_COL[g.tab]);
            const BInfo& in = binfo(cards[i]);
            rect(ren, b.x + 3, b.y + 3, b.w - 6, 3, afford ? TAB_COL[g.tab] : 0x45394a);
            centered(ren, b, b.y + 10, afford ? UI_TEXT : 0x6d5f70, shortName(cards[i]));
            centered(ren, b, b.y + 22, afford ? 0xe6c89a : UI_BAD, costStr(cards[i]));
            std::string meta = std::to_string(in.w) + "x" + std::to_string(in.h) + (in.workers ? " " + std::to_string(in.workers) + "w" : "");
            centered(ren, b, b.y + 33, afford ? UI_DIM : 0x6d5f70, meta);
        }
    }
    // confirm row: status text on the left, Cancel / Build on the right (no stray taps waste goods)
    if (confirmRow) {
        float bw = 56.f;
        SDL_FRect ok{g.scrW - pad - bw, confY, bw, btnH}, no{g.scrW - 2 * pad - 2 * bw, confY, bw, btnH};
        std::string status; uint32_t scol = UI_TEXT;
        std::string okLabel = "Build";
        bool showOk = true;
        if (g.tool == Tool::Place) {
            bool can = w.canPlace(g.placing, g.gx, g.gy), aff = w.canAfford(g.placing);
            status = !aff ? "Need " + costStr(g.placing) : (can ? "Drag to move" : (g.placing == BType::Fisher ? "Needs water" : "Blocked"));
            scol = can && aff ? UI_DIM : UI_BAD;
        } else if (g.tool == Tool::Road) {
            int c = g.roadCost();
            status = c ? "Road: " + std::to_string(c) + " logs" : "Drag to draw";
            scol = c > w.store[(int)Res::Logs] ? UI_BAD : UI_DIM;
            okLabel = "Done"; showOk = true;
        } else if (g.tool == Tool::Remove) {
            status = g.remX >= 0 ? "Half refund" : "Tap to pick";
            okLabel = "Remove"; showOk = g.remX >= 0;
        } else if (g.selected >= 0) {
            const Building& b = w.buildings[g.selected];
            const BInfo& in = binfo(b.type);
            status = shortName(b.type);
            if (!b.connected && b.type != BType::Hall) { status += ": no road"; scol = UI_BAD; }
            else if (in.workers) status += " " + std::to_string(b.staffed) + "/" + std::to_string(in.workers) + " workers";
            else if (b.type == BType::House) status += b.hungry ? ": hungry" : (b.cold ? ": cold" : ": 4 beds");
            showOk = false;
        }
        if (draw) text(ren, pad, confY + 10, scol, status);
        if (g.tool != Tool::None || g.selected >= 0) {
            g.btns.push_back({no, B_CANCEL});
            if (draw) { button(ren, no, UI_PANEL, false, 0); centered(ren, no, no.y + 10, UI_DIM, g.tool == Tool::None ? "Close" : "Cancel"); }
        }
        if (showOk && g.tool != Tool::None) {
            g.btns.push_back({ok, B_OK});
            bool good = g.tool != Tool::Place || (w.canPlace(g.placing, g.gx, g.gy) && w.canAfford(g.placing));
            if (draw) { button(ren, ok, good ? 0x1a7a4c : 0x5a2a35, true, good ? UI_GOOD : UI_BAD); centered(ren, ok, ok.y + 10, UI_TEXT, okLabel); }
        }
    }
    g.viewY = (int)topH; g.viewW = g.scrW; g.viewH = std::max(40, (int)drawerTop - (int)topH);

    if (!draw) return;
    // ---- disaster warning: tint or edge glow on the map, and a countdown chip under the top bar
    float logY = topH + 3.f;
    if (w.pending != Event::None) {
        uint32_t c = eventColor(w.pending);
        float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() * 0.008f);
        uint8_t cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
        if (w.pendingSide != 0) {   // glow on the edge they come from
            for (int i = 0; i < 24; i++) {
                float a = (1.f - i / 24.f) * (60.f + 70.f * pulse);
                float x = w.pendingSide < 0 ? (float)i : (float)(g.viewW - 1 - i);
                SDL_SetRenderDrawColor(ren, cr, cg, cb, (uint8_t)a);
                SDL_FRect fr{x, (float)g.viewY, 1.f, (float)g.viewH}; SDL_RenderFillRect(ren, &fr);
            }
        } else {
            SDL_SetRenderDrawColor(ren, cr, cg, cb, (uint8_t)(18.f + 22.f * pulse));
            SDL_FRect fr{0, (float)g.viewY, (float)g.viewW, (float)g.viewH}; SDL_RenderFillRect(ren, &fr);
        }
        int secs = (w.pendingTicks + 9) / 10;
        std::string msg = std::string(World::warnText(w.pending, w.pendingSide)) + "  " + std::to_string(secs) + "s";
        float cw = 8.f * msg.size() + 12.f, cx = pad, cy = topH + 3.f;
        rect(ren, cx, cy, cw, 15, 0x14101a, 235);
        rect(ren, cx, cy, 3, 15, c);
        rect(ren, cx + 3, cy + 14, (cw - 3) * w.pendingTicks / (float)WARN_TICKS, 1, c);
        text(ren, cx + 8, cy + 4, c, msg);
        logY = cy + 19.f;
    }
    // ---- event log under the top bar
    size_t maxChars = (size_t)std::max(8, (g.scrW - 8) / 8);
    int shown = 0;
    for (int i = (int)w.log.size() - 1; i >= 0 && shown < 3; i--) {
        const LogLine& l = w.log[i];
        if (w.day() + 1 - l.day > 2) break;
        std::string s = "D" + std::to_string(l.day) + " " + l.text;
        if (s.size() > maxChars) s = s.substr(0, maxChars);
        float ly = logY + shown * 10.f;
        rect(ren, 1, ly - 1, 8.f * s.size() + 4, 10, 0x000000, 140);
        text(ren, 3, ly, l.text.find('!') != std::string::npos ? UI_WARN : 0xc7dcd0, s);
        shown++;
    }
    {   // zoom buttons, right edge of the map just above the drawer
        float bs = 30.f, bx = g.scrW - pad - bs, by = drawerTop - pad - bs;
        SDL_FRect zo{bx, by, bs, bs}, zi{bx, by - bs - 4.f, bs, bs};
        g.btns.push_back({zi, B_ZIN}); g.btns.push_back({zo, B_ZOUT});
        bool canIn = g.zoom < g.maxZoom() - 1e-3f, canOut = g.zoom > g.minZoom() + 1e-3f;
        button(ren, zi, UI_PANEL, false, UI_TEXT); text(ren, zi.x + 7, zi.y + 7, canIn ? UI_TEXT : UI_DIM, "+", 2.f);
        button(ren, zo, UI_PANEL, false, UI_TEXT); text(ren, zo.x + 7, zo.y + 7, canOut ? UI_TEXT : UI_DIM, "-", 2.f);
    }
    if (!g.toast.empty() && SDL_GetTicks() < g.toastUntil) {
        float tw = 8.f * g.toast.size() + 10.f, tx = (g.scrW - tw) / 2.f, ty = drawerTop - 18.f;
        rect(ren, tx, ty, tw, 13, 0x14101a, 220);
        text(ren, tx + 5, ty + 3, UI_WARN, g.toast);
    }
    // ---- off-screen danger arrows: orange fire, green plague, red raiders
    {
        struct Acc { float x = 0, y = 0; int n = 0; uint32_t col; } acc[3];
        acc[0].col = 0xfb6b1d; acc[1].col = 0x9cdb43; acc[2].col = 0xe83b3b;
        float vx0 = g.camX, vy0 = g.camY, vx1 = g.camX + (float)g.viewW / g.zoom, vy1 = g.camY + (float)g.viewH / g.zoom;
        auto add = [&](int k, float px, float py) { if (px < vx0 || px >= vx1 || py < vy0 || py >= vy1) { acc[k].x += px; acc[k].y += py; acc[k].n++; } };
        for (int ty = 0; ty < MAP_H; ty++) for (int tx = 0; tx < MAP_W; tx++) if (w.at(tx, ty).fire > 0.f) add(0, tx * TILE_PX + 8.f, ty * TILE_PX + 8.f);
        for (auto& v : w.villagers) if (v.alive && v.sick) add(1, v.x * TILE_PX + 8.f, v.y * TILE_PX + 8.f);
        for (auto& r : w.raiders) if (r.alive) add(2, r.x * TILE_PX + 8.f, r.y * TILE_PX + 8.f);
        float cx = g.viewW / 2.f, cy = g.viewY + g.viewH / 2.f;
        for (auto& a : acc) {
            if (!a.n) continue;
            float sx2 = (a.x / a.n - g.camX) * g.zoom, sy2 = (a.y / a.n - g.camY) * g.zoom + g.viewY;
            float dx = sx2 - cx, dy = sy2 - cy, len = std::hypot(dx, dy);
            if (len < 1.f) continue;
            dx /= len; dy /= len;
            float m = 10.f;
            float t = std::min(std::abs(dx) > 1e-4f ? (g.viewW / 2.f - m) / std::abs(dx) : 1e9f,
                               std::abs(dy) > 1e-4f ? (g.viewH / 2.f - m) / std::abs(dy) : 1e9f);
            float ax = cx + dx * t, ay = cy + dy * t, px = -dy, py = dx;
            auto tri = [&](float s, uint32_t c) {
                SDL_FColor col{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1.f};
                SDL_Vertex vt[3] = {{{ax + dx * 6 * s, ay + dy * 6 * s}, col, {0, 0}},
                                    {{ax - dx * 4 * s + px * 5 * s, ay - dy * 4 * s + py * 5 * s}, col, {0, 0}},
                                    {{ax - dx * 4 * s - px * 5 * s, ay - dy * 4 * s - py * 5 * s}, col, {0, 0}}};
                SDL_RenderGeometry(ren, nullptr, vt, 3, nullptr, 0);
            };
            tri(1.35f, 0x14101a); tri(1.f, a.col);
        }
    }
    if (w.gameOver()) {
        char buf[96];
        float bw = std::min(262.f, g.scrW - 8.f), bx = (g.scrW - bw) / 2;
        size_t cols = (size_t)((bw - 16.f) / 8.f);
        // the town's story: drop "first X" lines, then the oldest middle ones, until it fits
        struct Entry { const LogLine* l; std::vector<std::string> lines; };
        std::vector<Entry> es;
        for (auto& l : w.history) { std::string d = "Day " + std::to_string(l.day); es.push_back({&l, wrap(d + std::string(6 - std::min<size_t>(5, d.size()), ' ') + l.text, cols)}); }
        float fixedH = 8 + 12 + 12 + 12 + 8 + 34 + 8;   // title, stats, gap, buttons
        float maxH = g.viewH - 8.f;
        auto height = [&]() { float h = fixedH; for (auto& e : es) h += e.lines.size() * 10.f + 2.f; return h; };
        for (int pass = 0; pass < 2 && height() > maxH; pass++)
            for (size_t i = 1; i + 1 < es.size() && height() > maxH;)
                if (pass == 1 || es[i].l->text.rfind("Built the first", 0) == 0) es.erase(es.begin() + i); else i++;
        float bh = std::min(maxH, height()), by = g.viewY + (g.viewH - bh) / 2;
        rect(ren, bx, by, bw, bh, UI_BG, 245);
        rect(ren, bx, by, bw, 1, 0x5d4b62);
        text(ren, bx + 8, by + 8, UI_BAD, g.daily ? "DAILY RUN OVER" : "YOUR TOWN FELL");
        int best = g.daily ? g.bestDaily : g.best;
        std::snprintf(buf, sizeof(buf), "Survived %d days  Best %d", w.day(), best); text(ren, bx + 8, by + 20, UI_TEXT, buf);
        std::snprintf(buf, sizeof(buf), "Born %d  Died %d", w.births, w.deaths); text(ren, bx + 8, by + 32, UI_DIM, buf);
        float ly = by + 50;
        for (size_t i = 0; i < es.size(); i++) {
            bool last = i + 1 == es.size(), bad = es[i].l->text.find(" took ") != std::string::npos;
            uint32_t col = last ? UI_BAD : bad ? UI_WARN : (es[i].l->text.rfind("Built", 0) == 0 ? UI_DIM : 0xc7dcd0);
            for (size_t k = 0; k < es[i].lines.size() && ly < by + bh - 44; k++, ly += 10) text(ren, bx + 8, ly, col, (k ? "      " : "") + es[i].lines[k]);
            ly += 2;
        }
        float half = (bw - 16 - 6) / 2;
        SDL_FRect b{bx + 8, by + bh - 34, half, 28}, d{bx + 14 + half, by + bh - 34, half, 28};
        g.btns.push_back({b, B_RESTART}); g.btns.push_back({d, B_DAILY});
        button(ren, b, 0x1a7a4c, true, UI_GOOD); centered(ren, b, b.y + 10, UI_TEXT, "New world");
        button(ren, d, UI_PANEL, true, UI_WARN); centered(ren, d, d.y + 10, UI_WARN, "Daily");
    }
}

static int hitButton(const Game& g, float lx, float ly) {
    SDL_FPoint p{lx, ly};
    for (int i = (int)g.btns.size() - 1; i >= 0; i--) if (SDL_PointInRectFloat(&p, &g.btns[i].r)) return g.btns[i].id;
    return -1;
}

static void pressButton(Game& g, int id) {
    World& w = *g.world;
    if (id == B_SPEED) g.cycleSpeed();
    else if (id == B_ZIN) g.stepZoomCenter(+1);
    else if (id == B_ZOUT) g.stepZoomCenter(-1);
    else if (id == B_RESTART) g.restart(g.daily ? todaySeed() + 7919u * (uint32_t)SDL_GetTicks() : g.seed + 1);
    else if (id == B_DAILY) g.restart(todaySeed(), true);
    else if (id >= B_TAB0 && id < B_TAB0 + 4) { g.tab = g.tab == id - B_TAB0 ? -1 : id - B_TAB0; if (g.tool != Tool::Place) g.cancelTool(); }
    else if (id == B_ROAD) { bool on = g.tool == Tool::Road; g.cancelTool(); g.selected = -1; if (!on) { g.tool = Tool::Road; g.tab = -1; } }
    else if (id == B_REMOVE) { bool on = g.tool == Tool::Remove; g.cancelTool(); g.selected = -1; if (!on) { g.tool = Tool::Remove; g.tab = -1; } }
    else if (id >= B_CARD0 && id < B_CARD0 + (int)BType::Count) g.startPlace((BType)(id - B_CARD0));
    else if (id == B_CANCEL) { g.cancelTool(); g.selected = -1; }
    else if (id == B_OK) {
        if (g.tool == Tool::Place) g.confirmPlace();
        else if (g.tool == Tool::Road) { if (!g.roadDrag.empty()) g.commitRoad(); g.cancelTool(); }
        else if (g.tool == Tool::Remove && g.remX >= 0) { if (!w.demolish(g.remX, g.remY)) g.say("The hall stays"); g.remX = g.remY = -1; }
    }
}

// A tap (or click) on the map, at the point the tool acts on.
static void mapTap(Game& g, float lx, float ly, bool mouse) {
    World& w = *g.world;
    int tx, ty; g.toTile(lx, ly, tx, ty);
    switch (g.tool) {
    case Tool::Place: g.moveGhost(lx, ly); if (mouse) g.confirmPlace(); break;
    case Tool::Remove:
        if (w.inside(tx, ty) && (w.at(tx, ty).t == Tile::Road || w.buildingAt(tx, ty) > 0)) {
            if (mouse || (g.remX == tx && g.remY == ty)) { w.demolish(tx, ty); g.remX = g.remY = -1; }
            else { g.remX = tx; g.remY = ty; }
        }
        break;
    case Tool::Road: g.roadDrag.clear(); g.addRoadTile(tx, ty); g.commitRoad(); break;
    case Tool::None: g.selected = w.inside(tx, ty) ? w.buildingAt(tx, ty) : -1; break;
    }
}

// Touch: one finger acts with the current tool (lifted above the thumb so you can see the spot),
// or pans when no tool is active; two fingers always pan + pinch zoom.
struct TouchState {
    SDL_FingerID id[2]{}; float x[2]{}, y[2]{};
    int n = 0; float downX = 0, downY = 0; bool moved = false, onUi = false;
    float pinchStart = 0; float zoomStart = 1; float midX = 0, midY = 0;
    static constexpr float LIFT = 24.f;
};
static TouchState touch;

static void fingerToLogical(SDL_Renderer* ren, const SDL_TouchFingerEvent& f, float& lx, float& ly) {
    int ww = 0, wh = 0; SDL_GetWindowSize(SDL_GetRenderWindow(ren), &ww, &wh);
    SDL_RenderCoordinatesFromWindow(ren, f.x * ww, f.y * wh, &lx, &ly);
}

static void handleTouch(SDL_Renderer* ren, Game& g, const SDL_Event& e) {
    const SDL_TouchFingerEvent& f = e.tfinger;
    float lx, ly; fingerToLogical(ren, f, lx, ly);
    int slot = -1;
    for (int i = 0; i < touch.n; i++) if (touch.id[i] == f.fingerID) slot = i;
    float aimY = ly - TouchState::LIFT;
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (touch.n >= 2) return;
        slot = touch.n++;
        touch.id[slot] = f.fingerID; touch.x[slot] = lx; touch.y[slot] = ly;
        if (touch.n == 1) {
            touch.downX = lx; touch.downY = ly; touch.moved = false;
            touch.onUi = hitButton(g, lx, ly) >= 0 || !g.inView(lx, ly);
            if (!touch.onUi && g.tool == Tool::Place) g.moveGhost(lx, aimY);
            if (!touch.onUi && g.tool == Tool::Road) { g.roadDrag.clear(); g.painting = true; int tx, ty; g.toTile(lx, aimY, tx, ty); g.addRoadTile(tx, ty); }
        } else {
            touch.moved = true;
            if (g.painting) { g.roadDrag.clear(); g.painting = false; }   // second finger cancels the stroke
            touch.pinchStart = std::hypot(touch.x[1] - touch.x[0], touch.y[1] - touch.y[0]);
            touch.zoomStart = g.zoom;
            touch.midX = (touch.x[0] + touch.x[1]) / 2; touch.midY = (touch.y[0] + touch.y[1]) / 2;
        }
    } else if (e.type == SDL_EVENT_FINGER_MOTION && slot >= 0) {
        float dx = lx - touch.x[slot], dy = ly - touch.y[slot];
        touch.x[slot] = lx; touch.y[slot] = ly;
        if (touch.n == 1) {
            if (touch.onUi) return;
            if (std::hypot(lx - touch.downX, ly - touch.downY) > 6.f) touch.moved = true;
            if (g.tool == Tool::Place) g.moveGhost(lx, aimY);
            else if (g.tool == Tool::Road && g.painting) { int tx, ty; g.toTile(lx, aimY, tx, ty); g.addRoadTile(tx, ty); }
            else if (touch.moved) { g.camX -= dx / g.zoom; g.camY -= dy / g.zoom; g.clampCam(); }
        } else {
            float mx = (touch.x[0] + touch.x[1]) / 2, my = (touch.y[0] + touch.y[1]) / 2;
            g.camX -= (mx - touch.midX) / g.zoom; g.camY -= (my - touch.midY) / g.zoom; g.clampCam();
            touch.midX = mx; touch.midY = my;
            if (touch.pinchStart > 1.f) {
                float d = std::hypot(touch.x[1] - touch.x[0], touch.y[1] - touch.y[0]);
                float want = touch.zoomStart * d / touch.pinchStart, z = g.ZL[0];   // snap to the nearest step
                for (float s : g.ZL) if (std::abs(std::log2(s / want)) < std::abs(std::log2(z / want))) z = s;
                if (z != g.zoom) g.setZoom(z, mx, my);
            }
        }
    } else if (e.type == SDL_EVENT_FINGER_UP && slot >= 0) {
        if (touch.n == 1) {
            if (touch.onUi) { if (!touch.moved) { int id = hitButton(g, lx, ly); if (id >= 0) pressButton(g, id); } }
            else if (g.tool == Tool::Road && g.painting) g.commitRoad();
            else if (!touch.moved && g.tool != Tool::Place) mapTap(g, lx, g.tool == Tool::None ? ly : aimY, false);
        }
        touch.id[slot] = touch.id[touch.n - 1]; touch.x[slot] = touch.x[touch.n - 1]; touch.y[slot] = touch.y[touch.n - 1];
        touch.n--;
        if (touch.n == 1) { touch.downX = touch.x[0]; touch.downY = touch.y[0]; touch.onUi = true; }   // finish the gesture quietly
    }
}

static void relayout(SDL_Renderer* ren, Game& g) {
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    int scale = std::max(1, std::min(pw, ph) / 270);   // short side ~270 logical px, whole-number scale
    g.buildZoomSteps(scale);
    g.scrW = std::max(160, pw / scale); g.scrH = std::max(160, ph / scale);
    SDL_SetRenderLogicalPresentation(ren, g.scrW, g.scrH, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_Window* win = SDL_GetRenderWindow(ren);
    int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh);
    SDL_Rect sa{0, 0, ww, wh};
    g.safeTop = g.safeBottom = 0;
    if (wh > 0 && SDL_GetWindowSafeArea(win, &sa)) {
        float k = (float)g.scrH / wh;
        g.safeTop = (int)std::ceil(sa.y * k); g.safeBottom = (int)std::ceil((wh - sa.y - sa.h) * k);
    }
    ui(ren, g, false);
    g.clampCam();
}

int main(int argc, char** argv) {
    const char* shotPath = nullptr;
    int shotDays = 0; bool demo = false; float shotZoom = 0.f; bool daily = false;
    std::string shotUi = "well";
    int winW = 324, winH = 702;   // portrait phone shape on desktop
    uint32_t seed = (uint32_t)std::time(nullptr);
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--days") && i + 1 < argc) shotDays = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--demo")) demo = true;
        else if (!std::strcmp(argv[i], "--daily")) daily = true;
        else if (!std::strcmp(argv[i], "--zoom") && i + 1 < argc) shotZoom = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--ui") && i + 1 < argc) shotUi = argv[++i];
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("villagesim [--seed N] [--size WxH]\n"
                        "  --shot out.bmp [--days N] [--demo] [--ui none|well|road|info|warn] [--daily] [--zoom 0.5..4]  render one frame and exit\n"
                        "  --demo  a scripted player builds the town during --days (for screenshots)\n");
            return 0;
        }
    }
    if (shotPath) { SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"); SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"); }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");   // vertical-only game
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr;
    if (!SDL_CreateWindowAndRenderer("A2D Village Sim", winW, winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    Game g;
    g.best = loadBest(); g.bestDaily = loadBest(true);
    g.restart(daily ? todaySeed() : seed, daily);
    relayout(ren, g);
    g.zoom = std::max(1.f, g.minZoom());
    if (shotZoom > 0.f) { g.zoom = shotZoom; g.zoom = g.ZL[g.zoomIndex()]; }
    g.centerOnHall();
    if (shotPath) {
        World& w = *g.world;
        for (int d = 0; d < shotDays && !w.gameOver(); d++) {
            if (demo) autoplay::defender(w);
            for (int t = 0; t < TICKS_PER_DAY && !w.gameOver(); t++) w.tick();
        }
        for (int t = 0; t < TICKS_PER_DAY / 3 && !w.gameOver(); t++) w.tick();   // midday light
        if (shotUi == "warn") { w.pending = Event::Raiders; w.pendingTicks = 50; w.pendingSide = -1; }
        if (shotUi == "warn2") { w.pending = Event::Wildfire; w.pendingTicks = 60; w.pendingSide = 0; }
        if (shotUi == "well" || shotUi == "tower") {
            g.tab = 3;
            BType t = shotUi == "well" ? BType::Well : BType::Tower;
            w.store[(int)Res::Logs] = std::max(w.store[(int)Res::Logs], 10.f); w.store[(int)Res::Planks] = std::max(w.store[(int)Res::Planks], 10.f);
            g.tool = Tool::Place; g.placing = t;
            // find a free spot near the houses
            float bestD = 1e9f;
            for (int y = 0; y < MAP_H - 2; y++) for (int x = 0; x < MAP_W - 2; x++)
                if (w.canPlace(t, x, y)) { float d = std::hypot(x - (w.hallX + 1.f), y - (w.hallY + 6.f)); if (d < bestD) { bestD = d; g.gx = x; g.gy = y; } }
        } else if (shotUi == "road") {
            g.tool = Tool::Road;
            for (int i = 0; i < 8; i++) g.addRoadTile(w.hallX + 4 + i, w.hallY + 8);
        } else if (shotUi == "info") {
            g.tab = 1;
            for (int i = 0; i < (int)w.buildings.size(); i++) if (w.buildings[i].alive && w.buildings[i].type == BType::Mill) g.selected = i;
        }
        ui(ren, g, false);
        g.centerOn((w.hallX + 1.5f) * TILE_PX, (w.hallY + 3.f) * TILE_PX);
    }

    uint64_t last = SDL_GetTicks();
    double acc = 0.0;
    int frame = 0;
    bool running = true, mouseDown = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) relayout(ren, g);
            else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_BACKGROUND) g.paused = true;
            else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP) handleTouch(ren, g, e);
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) { if (g.tool != Tool::None || g.selected >= 0 || g.tab >= 0) { g.cancelTool(); g.selected = -1; g.tab = -1; } else running = false; }
                else if (k == SDLK_SPACE) g.paused = !g.paused;
                else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) g.speed = std::min(8, g.speed * 2);
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) g.speed = std::max(1, g.speed / 2);
                else if (k == SDLK_H) g.centerOnHall();
                else if (k == SDLK_R && (g.world->gameOver() || (e.key.mod & SDL_KMOD_SHIFT))) g.restart(g.seed + 1);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                float lx, ly; SDL_RenderCoordinatesFromWindow(ren, e.button.x, e.button.y, &lx, &ly);
                int id = hitButton(g, lx, ly);
                if (id >= 0) pressButton(g, id);
                else if (g.inView(lx, ly)) {
                    if (g.tool == Tool::Road) { mouseDown = true; g.roadDrag.clear(); g.painting = true; int tx, ty; g.toTile(lx, ly, tx, ty); g.addRoadTile(tx, ty); }
                    else mapTap(g, lx, ly, true);
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                if (mouseDown && g.painting) g.commitRoad();
                mouseDown = false;
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                float lx, ly; SDL_RenderCoordinatesFromWindow(ren, e.wheel.mouse_x, e.wheel.mouse_y, &lx, &ly);
                if (e.wheel.y > 0) g.stepZoom(+1, lx, ly); else if (e.wheel.y < 0) g.stepZoom(-1, lx, ly);
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                float lx, ly; SDL_RenderCoordinatesFromWindow(ren, e.motion.x, e.motion.y, &lx, &ly);
                if (e.motion.state & (SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) {
                    float ax, ay, bx, by;
                    SDL_RenderCoordinatesFromWindow(ren, 0, 0, &ax, &ay);
                    SDL_RenderCoordinatesFromWindow(ren, e.motion.xrel, e.motion.yrel, &bx, &by);
                    g.camX -= (bx - ax) / g.zoom; g.camY -= (by - ay) / g.zoom; g.clampCam();
                } else if (g.inView(lx, ly)) {
                    if (g.tool == Tool::Place) g.moveGhost(lx, ly);   // ghost follows the mouse
                    else if (g.tool == Tool::Road && mouseDown) { int tx, ty; g.toTile(lx, ly, tx, ty); g.addRoadTile(tx, ty); }
                }
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
        acc += (now - last) / 1000.0; last = now;
        if (acc > 0.5) acc = 0.5;
        while (acc >= 0.1) { acc -= 0.1; if (!g.paused) for (int s = 0; s < g.speed; s++) g.world->tick(); }
        if (g.world->gameOver() && !g.bestSaved) {
            int& best = g.daily ? g.bestDaily : g.best;
            if (g.world->day() > best) { best = g.world->day(); saveBest(best, g.daily); }
            g.bestSaved = true;
        }
        if (g.world->pending != g.warnedFor) {   // a new warning: slow down so there's time to react
            if (g.world->pending != Event::None && g.speed > 1) g.speed = 1;
            g.warnedFor = g.world->pending;
        }
        if (g.selected >= 0 && (g.selected >= (int)g.world->buildings.size() || !g.world->buildings[g.selected].alive)) g.selected = -1;

        Overlay ov;
        if (g.tool == Tool::Place) { ov.ghost = (int)g.placing; ov.gx = g.gx; ov.gy = g.gy; ov.ghostOk = g.world->canPlace(g.placing, g.gx, g.gy) && g.world->canAfford(g.placing); }
        ov.roadMode = g.tool == Tool::Road; ov.roadTiles = g.roadDrag;
        if (g.tool == Tool::Remove) { ov.demolishX = g.remX; ov.demolishY = g.remY; }
        ov.selected = g.selected;
        ov.markScale = g.zoom < 0.99f ? (int)std::ceil(1.f / g.zoom - 0.01f) : 1;   // keep need bubbles readable when zoomed out

        ui(ren, g, false);   // measure first so the map viewport is right this frame
        g.clampCam();
        ov.vx0 = (int)g.camX - 1; ov.vy0 = (int)g.camY - 1;
        ov.vx1 = (int)(g.camX + (float)g.viewW / g.zoom) + 2; ov.vy1 = (int)(g.camY + (float)g.viewH / g.zoom) + 2;
        void* pixels; int pitch;
        if (SDL_LockTexture(tex, nullptr, &pixels, &pitch)) { drawWorld(*g.world, (uint32_t*)pixels, pitch / 4, frame, ov); SDL_UnlockTexture(tex); }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_FRect dst{0, (float)g.viewY, (float)g.viewW, (float)g.viewH};
        SDL_FRect src{g.camX, g.camY, (float)g.viewW / g.zoom, (float)g.viewH / g.zoom};
        SDL_RenderTexture(ren, tex, &src, &dst);
        ui(ren, g, true);
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
