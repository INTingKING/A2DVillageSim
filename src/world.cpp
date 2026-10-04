#include "world.hpp"
#include <algorithm>
#include <cmath>
#include <queue>

namespace sim {

namespace {
enum Task { T_IDLE = 0, T_TO_WORK, T_AT_WORK, T_TO_RES, T_GATHER, T_RETURN, T_WANDER };

const BInfo kB[(int)BType::Count] = {
    // name        w  h  logs planks workers radius category          desc
    {"Town Hall",  3, 3,  0,  0, 0, 0.f, Category::Home,   "Storage and shelter for 6. Roads must connect to it."},
    {"House",      2, 2,  0,  4, 0, 0.f, Category::Home,   "Homes 4 villagers. Needs a road."},
    {"Lumber Camp",2, 2,  6,  0, 1, 6.f, Category::Wood,   "Chops nearby trees into logs."},
    {"Sawmill",    2, 2,  8,  0, 1, 0.f, Category::Wood,   "Logs -> planks."},
    {"Fisher",     2, 2,  4,  2, 1, 5.f, Category::Food,   "Catches fish. Must be near water."},
    {"Wheat Farm", 3, 3,  2,  4, 1, 0.f, Category::Food,   "Grows wheat in spring to autumn."},
    {"Windmill",   2, 2,  4,  8, 1, 0.f, Category::Food,   "Wheat -> flour."},
    {"Bakery",     2, 2,  4,  8, 1, 0.f, Category::Food,   "Flour -> bread (2 per flour)."},
    {"Well",       1, 1,  2,  2, 0, 5.f, Category::Safety, "Puts out fires in its circle."},
    {"Healer",     2, 2,  0, 12, 1, 8.f, Category::Safety, "Cures plague and stops it spreading."},
    {"Watchtower", 2, 2,  6,  6, 1, 7.f, Category::Safety, "Shoots raiders in range."},
};

bool walkable(Tile t) {
    return t == Tile::Sand || t == Tile::Grass || t == Tile::Forest || t == Tile::Ash || t == Tile::Road;
}
bool buildable(Tile t) { return t == Tile::Grass || t == Tile::Sand || t == Tile::Ash; }

float hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / (float)0xffffff;
}
float smooth(float t) { return t * t * (3.f - 2.f * t); }
float vnoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = smooth(x - xi), fy = smooth(y - yi);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float fbm(float x, float y, uint32_t seed) {
    float s = 0.f, amp = 0.5f, f = 1.f;
    for (int o = 0; o < 5; o++) { s += amp * vnoise(x * f, y * f, seed + o * 101); amp *= 0.5f; f *= 2.f; }
    return s;
}
const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
} // namespace

const BInfo& binfo(BType t) { return kB[(int)t]; }
const char* resName(Res r) {
    static const char* n[] = {"Logs", "Planks", "Wheat", "Flour", "Bread", "Fish"};
    return n[(int)r];
}
const char* seasonName(Season s) {
    static const char* n[] = {"Spring", "Summer", "Autumn", "Winter"};
    return n[(int)s];
}
const char* categoryName(Category c) {
    static const char* n[] = {"Homes", "Food", "Wood", "Safety"};
    return n[(int)c];
}

World::World(uint32_t seed, bool disasters) : disastersOn(disasters), rng(seed) {
    generate();
    note("The town was founded by " + std::to_string(population()) + " villagers.");
    popMark = population();
}

int World::dangerLevel() const { return day() / 10; }

const char* World::warnText(Event e, int side) {
    switch (e) {
    case Event::Raiders: return side < 0 ? "Raiders spotted in the west" : "Raiders spotted in the east";
    case Event::Wildfire: return "Smoke on the wind";
    case Event::Plague: return "A cough is spreading";
    case Event::Drought: return "The air is turning dry";
    case Event::Locusts: return "A swarm is coming";
    case Event::Blizzard: return "Dark clouds gather";
    default: return "";
    }
}

static const char* storyName(Event e) {
    switch (e) {
    case Event::Raiders: return "Raiders";
    case Event::Wildfire: return "A great fire";
    case Event::Plague: return "The plague";
    case Event::Drought: return "The drought";
    case Event::Locusts: return "The locusts";
    case Event::Blizzard: return "The blizzard";
    default: return "Hunger and cold";
    }
}

float World::frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
int World::irand(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

void World::say(const std::string& s) {
    log.push_back({day() + 1, s});
    if (log.size() > 60) log.erase(log.begin());
}

bool World::take(Res r, float n) {
    if (store[(int)r] < n) return false;
    store[(int)r] -= n; return true;
}

void World::generate() {
    cells.assign(MAP_W * MAP_H, Cell{});
    uint32_t seed = rng();
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            float nx = (x - MAP_W * 0.5f) / (MAP_W * 0.5f);
            float ny = (y - MAP_H * 0.5f) / (MAP_H * 0.5f);
            float fall = std::sqrt(nx * nx * 0.9f + ny * ny);
            float hgt = fbm(x * 0.045f, y * 0.045f, seed) - fall * 0.55f + 0.18f;
            float moist = fbm(x * 0.07f + 50.f, y * 0.07f, seed ^ 0x9e37u);
            Cell& c = at(x, y);
            c.height = hgt;
            if (hgt < 0.12f) c.t = Tile::Deep;
            else if (hgt < 0.22f) c.t = Tile::Water;
            else if (hgt < 0.26f) c.t = Tile::Sand;
            else if (hgt > 0.62f) c.t = Tile::Mountain;
            else if (moist > 0.52f) { c.t = Tile::Forest; c.res = 3.f; }
            else c.t = Tile::Grass;
        }
    // Hall: open land near the centre, ideally with forest and water within reach.
    int bx = MAP_W / 2 - 1, by = MAP_H / 2 - 1; float best = -1e9f;
    for (int y = 10; y < MAP_H - 13; y++)
        for (int x = 8; x < MAP_W - 11; x++) {
            bool ok = true;
            for (int dy = -1; dy <= 3 && ok; dy++)
                for (int dx = -1; dx <= 3 && ok; dx++) ok = at(x + dx, y + dy).t == Tile::Grass;
            if (!ok) continue;
            int land = 0, forest = 0, water = 0;
            for (int dy = -9; dy <= 11; dy++)
                for (int dx = -9; dx <= 11; dx++) {
                    if (!inside(x + dx, y + dy)) continue;
                    Tile t = at(x + dx, y + dy).t;
                    land += t == Tile::Grass; forest += t == Tile::Forest; water += t == Tile::Water;
                }
            float sc = land * 1.f + std::min(forest, 60) * 1.5f + std::min(water, 30) * 2.f
                     - std::hypot(x - MAP_W * 0.5f, y - MAP_H * 0.5f) * 2.f;
            if (sc > best) { best = sc; bx = x; by = y; }
        }
    hallX = bx; hallY = by;
    Building h; h.type = BType::Hall; h.x = bx; h.y = by;
    buildings.push_back(h);
    for (int dy = 0; dy < 3; dy++)
        for (int dx = 0; dx < 3; dx++) { Cell& c = at(bx + dx, by + dy); c.t = Tile::Building; c.bld = 0; c.res = 0; }
    // a short road under the hall to get started
    for (int dx = -2; dx <= 4; dx++) if (inside(bx + dx, by + 3) && buildable(at(bx + dx, by + 3).t)) at(bx + dx, by + 3).t = Tile::Road;
    store[(int)Res::Logs] = 40; store[(int)Res::Planks] = 30; store[(int)Res::Bread] = 40;
    for (int i = 0; i < 8; i++) spawnVillager(bx + 1 + frand(-1.f, 1.f), by + 3.2f, frand(16.f, 30.f));
    refresh();
    say("Build your town and keep it alive.");
}

int World::buildingAt(int x, int y) const {
    if (!inside(x, y)) return -1;
    const Cell& c = at(x, y);
    return c.t == Tile::Building ? c.bld : -1;
}

void World::doorOf(const Building& b, int& ox, int& oy) const {
    const BInfo& bi = binfo(b.type);
    // prefer a road next to it, then any walkable tile, scanning the bottom edge first
    int best = -1; int bxx = b.x + bi.w / 2, byy = b.y + bi.h;
    for (int pass = 0; pass < 2 && best < 0; pass++)
        for (int y = b.y - 1; y <= b.y + bi.h && best < 0; y++)
            for (int x = b.x - 1; x <= b.x + bi.w; x++) {
                bool edge = (y == b.y - 1 || y == b.y + bi.h) != (x == b.x - 1 || x == b.x + bi.w);
                if (!edge || !inside(x, y)) continue;
                Tile t = at(x, y).t;
                if (pass == 0 ? t == Tile::Road : walkable(t)) { best = y * MAP_W + x; break; }
            }
    if (best >= 0) { ox = best % MAP_W; oy = best / MAP_W; }
    else { ox = std::clamp(bxx, 0, MAP_W - 1); oy = std::clamp(byy, 0, MAP_H - 1); }
}

bool World::canAfford(BType t) const {
    const BInfo& b = binfo(t);
    return store[(int)Res::Logs] >= b.costLogs && store[(int)Res::Planks] >= b.costPlanks;
}

bool World::canPlace(BType t, int x, int y) const {
    if (t == BType::Hall) return false;
    const BInfo& b = binfo(t);
    for (int dy = 0; dy < b.h; dy++)
        for (int dx = 0; dx < b.w; dx++) {
            if (!inside(x + dx, y + dy)) return false;
            const Cell& c = at(x + dx, y + dy);
            if (!buildable(c.t) || c.fire > 0.f) return false;
        }
    if (t == BType::Fisher) {   // same rule the fisher uses to find water (circle around its centre)
        bool water = false;
        int r = (int)b.radius, cx = x + b.w / 2, cy = y + b.h / 2;
        for (int yy = cy - r; yy <= cy + r && !water; yy++)
            for (int xx = cx - r; xx <= cx + r && !water; xx++) {
                if (!inside(xx, yy) || at(xx, yy).t != Tile::Water || std::hypot(float(xx - cx), float(yy - cy)) > r) continue;
                for (int k = 0; k < 4; k++) if (inside(xx + DX[k], yy + DY[k]) && walkable(at(xx + DX[k], yy + DY[k]).t)) water = true;
            }
        if (!water) return false;
    }
    return true;
}

bool World::place(BType t, int x, int y) {
    if (over || !canPlace(t, x, y) || !canAfford(t)) return false;
    const BInfo& b = binfo(t);
    store[(int)Res::Logs] -= b.costLogs; store[(int)Res::Planks] -= b.costPlanks;
    // story-worthy firsts only (the basic camps and houses come too early to be interesting)
    if (!built[(int)t] && (t == BType::Mill || t == BType::Bakery || t == BType::Well || t == BType::Healer || t == BType::Tower)) {
        built[(int)t] = true;
        std::string n = b.name; for (char& ch : n) ch = (char)std::tolower((unsigned char)ch);
        note("Built the first " + n + ".");
    }
    Building nb; nb.type = t; nb.x = x; nb.y = y;
    int idx = -1;
    for (int i = 0; i < (int)buildings.size(); i++) if (!buildings[i].alive) { idx = i; break; }
    if (idx < 0) { idx = (int)buildings.size(); buildings.push_back(nb); } else buildings[idx] = nb;
    for (int dy = 0; dy < b.h; dy++)
        for (int dx = 0; dx < b.w; dx++) { Cell& c = at(x + dx, y + dy); c.t = Tile::Building; c.bld = (int16_t)idx; c.res = 0; }
    refresh();
    return true;
}

bool World::canRoad(int x, int y) const {
    return inside(x, y) && buildable(at(x, y).t) && at(x, y).fire <= 0.f;
}

bool World::placeRoad(int x, int y) {
    if (over || !canRoad(x, y) || store[(int)Res::Logs] < 1.f) return false;
    store[(int)Res::Logs] -= 1.f;
    at(x, y).t = Tile::Road;
    refresh();
    return true;
}

bool World::demolish(int x, int y) {
    if (!inside(x, y)) return false;
    Cell& c = at(x, y);
    if (c.t == Tile::Road) { c.t = Tile::Grass; store[(int)Res::Logs] += 0.5f; refresh(); return true; }
    int bi = buildingAt(x, y);
    if (bi <= 0) return false;   // the hall stays
    const BInfo& b = binfo(buildings[bi].type);
    store[(int)Res::Logs] += b.costLogs / 2; store[(int)Res::Planks] += b.costPlanks / 2;
    Building& bd = buildings[bi];
    for (int dy = 0; dy < b.h; dy++)
        for (int dx = 0; dx < b.w; dx++) { Cell& cc = at(bd.x + dx, bd.y + dy); cc.t = Tile::Grass; cc.bld = -1; cc.fire = 0.f; }
    bd.alive = false;
    refresh();
    return true;
}

void World::destroyBuilding(int bi, const char* why) {
    Building& bd = buildings[bi];
    if (!bd.alive || bd.type == BType::Hall) return;
    const BInfo& b = binfo(bd.type);
    for (int dy = 0; dy < b.h; dy++)
        for (int dx = 0; dx < b.w; dx++) { Cell& c = at(bd.x + dx, bd.y + dy); c.t = Tile::Ash; c.res = 4.f; c.bld = -1; c.fire = 0.f; }
    bd.alive = false; lostToday++;
    say(std::string("A ") + b.name + " " + why + ".");
    refresh();
}

void World::refresh() {
    dirty++;
    // road network reachable from the hall
    std::vector<uint8_t> net(MAP_W * MAP_H, 0);
    std::queue<int> q;
    const Building& hall = buildings[0];
    for (int y = hall.y - 1; y <= hall.y + 3; y++)
        for (int x = hall.x - 1; x <= hall.x + 3; x++)
            if (inside(x, y) && at(x, y).t == Tile::Road) { net[y * MAP_W + x] = 1; q.push(y * MAP_W + x); }
    while (!q.empty()) {
        int i = q.front(); q.pop();
        int x = i % MAP_W, y = i / MAP_W;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!inside(nx, ny)) continue;
            int ni = ny * MAP_W + nx;
            if (!net[ni] && at(nx, ny).t == Tile::Road) { net[ni] = 1; q.push(ni); }
        }
    }
    for (int bi = 0; bi < (int)buildings.size(); bi++) {
        Building& b = buildings[bi];
        if (!b.alive) continue;
        if (b.type == BType::Hall || b.type == BType::Well) { b.connected = true; continue; }
        const BInfo& in = binfo(b.type);
        bool c = false;
        for (int y = b.y - 1; y <= b.y + in.h && !c; y++)
            for (int x = b.x - 1; x <= b.x + in.w && !c; x++)
                if (inside(x, y) && net[y * MAP_W + x]) c = true;
        b.connected = c;
    }
    // coverage maps
    wellCover.assign(MAP_W * MAP_H, 0);
    healCover.assign(MAP_W * MAP_H, 0);
    for (const Building& b : buildings) {
        if (!b.alive || (b.type != BType::Well && b.type != BType::Healer)) continue;
        const BInfo& in = binfo(b.type);
        float cx = b.x + in.w * 0.5f - 0.5f, cy = b.y + in.h * 0.5f - 0.5f;
        int r = (int)std::ceil(in.radius) + 1;
        auto& cov = b.type == BType::Well ? wellCover : healCover;
        for (int y = (int)cy - r; y <= (int)cy + r; y++)
            for (int x = (int)cx - r; x <= (int)cx + r; x++)
                if (inside(x, y) && std::hypot(x - cx, y - cy) <= in.radius) cov[y * MAP_W + x] = 1;
    }
    assignJobs();
}

void World::assignJobs() {
    // drop jobs at dead or disconnected buildings, and extras
    std::vector<int> have(buildings.size(), 0);
    for (Villager& v : villagers) {
        if (!v.alive) continue;
        if (v.work >= 0) {
            Building& b = buildings[v.work];
            if (!b.alive || !b.connected || have[v.work] >= binfo(b.type).workers) { v.work = -1; v.task = T_IDLE; v.path.clear(); v.pathPos = 0; }
            else have[v.work]++;
        }
    }
    static const BType prio[] = {BType::Fisher, BType::Farm, BType::Lumber, BType::Mill, BType::Bakery,
                                 BType::Sawmill, BType::Tower, BType::Healer};
    for (BType t : prio)
        for (int bi = 0; bi < (int)buildings.size(); bi++) {
            Building& b = buildings[bi];
            if (!b.alive || b.type != t || !b.connected) continue;
            while (have[bi] < binfo(t).workers) {
                Villager* pick = nullptr;
                for (Villager& v : villagers) if (v.alive && v.work < 0 && v.age >= 6.f && !v.sick) { pick = &v; break; }
                if (!pick) break;
                pick->work = bi; pick->task = T_IDLE; pick->path.clear(); pick->pathPos = 0;
                have[bi]++;
            }
        }
    for (int bi = 0; bi < (int)buildings.size(); bi++) buildings[bi].staffed = have[bi];
}

int World::population() const { int n = 0; for (auto& v : villagers) n += v.alive; return n; }
int World::sickCount() const { int n = 0; for (auto& v : villagers) n += v.alive && v.sick; return n; }
int World::raidersAlive() const { int n = 0; for (auto& r : raiders) n += r.alive; return n; }
int World::count(BType t) const { int n = 0; for (auto& b : buildings) n += b.alive && b.type == t; return n; }
int World::housing() const {
    int n = 0;
    for (auto& b : buildings) {
        if (!b.alive) continue;
        if (b.type == BType::Hall) n += 6;
        else if (b.type == BType::House && b.connected) n += 4;
    }
    return n;
}
int World::jobsOpen() const {
    int n = 0;
    for (auto& b : buildings) if (b.alive && b.connected) n += binfo(b.type).workers - b.staffed;
    return n;
}

void World::spawnVillager(float x, float y, float age) {
    Villager v; v.x = x; v.y = y; v.age = age; v.maxAge = frand(55.f, 80.f);
    villagers.push_back(v);
}

void World::kill(Villager& v, const char* why) {
    if (!v.alive) return;
    v.alive = false; deaths++; diedToday++;
    say(std::string("A villager died: ") + why + ".");
}

bool World::nearestTile(int sx, int sy, Tile t, int r, int& ox, int& oy) const {
    float best = 1e9f;
    for (int y = sy - r; y <= sy + r; y++)
        for (int x = sx - r; x <= sx + r; x++) {
            if (!inside(x, y)) continue;
            const Cell& c = at(x, y);
            if (c.t != t || c.fire > 0.f) continue;
            if (t == Tile::Forest && c.res < 1.f) continue;
            float d = std::hypot((float)(x - sx), (float)(y - sy)) + hash2(x, y, ticks / 50) * 1.5f;
            if (d < best && d <= r + 1.5f) { best = d; ox = x; oy = y; }
        }
    return best < 1e8f;
}

static std::vector<int> bfsPath(const World& w, int sx, int sy, int tx, int ty) {
    std::vector<int> prev(MAP_W * MAP_H, -2);
    std::queue<int> q;
    int s = sy * MAP_W + sx, t = ty * MAP_W + tx;
    q.push(s); prev[s] = -1;
    while (!q.empty()) {
        int i = q.front(); q.pop();
        if (i == t) break;
        int x = i % MAP_W, y = i / MAP_W;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!w.inside(nx, ny)) continue;
            int ni = ny * MAP_W + nx;
            if (prev[ni] != -2) continue;
            if (!walkable(w.at(nx, ny).t) && ni != t) continue;
            prev[ni] = i; q.push(ni);
        }
    }
    std::vector<int> path;
    if (prev[t] == -2) return path;
    for (int i = t; i != -1; i = prev[i]) path.push_back(i);
    std::reverse(path.begin(), path.end());
    return path;
}

void World::updateVillager(int vi) {
    Villager& v = villagers[vi];
    v.age += 1.f / TICKS_PER_DAY;
    int vx = std::clamp((int)std::lround(v.x), 0, MAP_W - 1);
    int vy = std::clamp((int)std::lround(v.y), 0, MAP_H - 1);
    if (at(vx, vy).fire > 0.f) v.health -= 1.5f;
    if (v.health <= 0.f) { kill(v, at(vx, vy).fire > 0.f ? "burned" : (v.sick ? "plague" : "hunger or cold")); return; }

    auto goTo = [&](int tx, int ty, int task) {
        v.path = bfsPath(*this, vx, vy, tx, ty);
        v.pathPos = 0; v.tx = tx; v.ty = ty; v.task = task; v.timer = 0.f;
        if (v.path.empty()) { v.task = T_IDLE; return false; }
        return true;
    };

    if (v.pathPos < v.path.size()) {
        int ni = v.path[v.pathPos];
        float gx = (float)(ni % MAP_W), gy = (float)(ni / MAP_W);
        float dx = gx - v.x, dy = gy - v.y, d = std::hypot(dx, dy);
        float sp = (at(vx, vy).t == Tile::Road ? 0.15f : 0.09f) * (v.sick ? 0.6f : 1.f);
        if (d <= sp) { v.x = gx; v.y = gy; v.pathPos++; }
        else { v.x += dx / d * sp; v.y += dy / d * sp; }
        return;
    }

    if (v.work < 0) {
        if (v.task == T_WANDER && (v.timer += 1.f) < 30.f) return;
        int wx = std::clamp(hallX + irand(-4, 6), 0, MAP_W - 1), wy = std::clamp(hallY + irand(-3, 6), 0, MAP_H - 1);
        if (walkable(at(wx, wy).t)) goTo(wx, wy, T_WANDER); else v.task = T_WANDER;
        return;
    }
    Building& b = buildings[v.work];
    const BInfo& in = binfo(b.type);
    int doorX, doorY; doorOf(b, doorX, doorY);
    bool gatherer = b.type == BType::Lumber || b.type == BType::Fisher;

    switch (v.task) {
    case T_IDLE:
        goTo(doorX, doorY, T_TO_WORK);
        if (v.task == T_IDLE) v.task = T_AT_WORK;   // already there or unreachable
        return;
    case T_TO_WORK:
        v.task = T_AT_WORK; v.timer = 0.f;
        return;
    case T_AT_WORK: {
        if (std::hypot(v.x - doorX, v.y - doorY) > 1.5f) { v.task = T_IDLE; return; }
        if (gatherer) {
            int cx = b.x + in.w / 2, cy = b.y + in.h / 2, tx, ty;
            Tile want = b.type == BType::Lumber ? Tile::Forest : Tile::Water;
            if (!nearestTile(cx, cy, want, (int)in.radius, tx, ty)) return;
            // walk next to water, onto forest
            if (want == Tile::Water) {
                bool found = false;
                for (int k = 0; k < 4 && !found; k++) if (inside(tx + DX[k], ty + DY[k]) && walkable(at(tx + DX[k], ty + DY[k]).t)) { tx += DX[k]; ty += DY[k]; found = true; }
                if (!found) return;
            }
            goTo(tx, ty, T_TO_RES);
            return;
        }
        // stationary work at the building
        updateBuilding(v.work);
        return;
    }
    case T_TO_RES:
        v.task = T_GATHER; v.timer = 0.f;
        return;
    case T_GATHER: {
        if ((v.timer += 1.f) < (b.type == BType::Lumber ? 35.f : 45.f)) return;
        if (b.type == BType::Lumber) {
            Cell& c = at(v.tx, v.ty);
            if (c.t != Tile::Forest || c.res < 1.f) { v.task = T_AT_WORK; return; }
            c.res -= 1.f;
            if (c.res < 1.f) { c.t = Tile::Grass; c.res = 0.f; }
        }
        v.carrying = true;
        goTo(doorX, doorY, T_RETURN);
        return;
    }
    case T_RETURN:
        if (v.carrying && b.connected) store[(int)(b.type == BType::Lumber ? Res::Logs : Res::Fish)] += b.type == BType::Lumber ? 3.f : 3.f;
        v.carrying = false;
        v.task = T_AT_WORK;
        return;
    default:
        v.task = T_IDLE;
    }
}

// One tick of work by one worker standing at the building.
void World::updateBuilding(int bi) {
    Building& b = buildings[bi];
    if (!b.connected) return;
    switch (b.type) {
    case BType::Farm:
        if (b.grow >= 1.f) {
            if ((b.work += 1.f) >= 20.f) { b.work = 0.f; b.grow = 0.f; store[(int)Res::Wheat] += 10.f; }
        }
        break;
    case BType::Mill:
        if (store[(int)Res::Wheat] >= 2.f && (b.work += 1.f) >= 40.f) { b.work = 0.f; store[(int)Res::Wheat] -= 2.f; store[(int)Res::Flour] += 2.f; }
        break;
    case BType::Bakery:
        if (store[(int)Res::Flour] >= 1.f && (b.work += 1.f) >= 30.f) { b.work = 0.f; store[(int)Res::Flour] -= 1.f; store[(int)Res::Bread] += 2.f; }
        break;
    case BType::Sawmill:
        if (store[(int)Res::Logs] >= 9.f && (b.work += 1.f) >= 25.f)   // keeps a few logs for firewood and roads
            { b.work = 0.f; store[(int)Res::Logs] -= 1.f; store[(int)Res::Planks] += 1.f; }
        break;
    case BType::Tower: {
        if ((b.cooldown -= 1.f) > 0.f) break;
        const BInfo& in = binfo(b.type);
        float cx = b.x + 1.f, cy = b.y + 0.5f;
        Raider* tgt = nullptr; float bd = in.radius;
        for (Raider& r : raiders) {
            if (!r.alive) continue;
            float d = std::hypot(r.x - cx, r.y - cy);
            if (d <= bd) { bd = d; tgt = &r; }
        }
        if (tgt) {
            tgt->health -= 25.f; b.cooldown = 12.f;
            arrows.push_back({cx, cy, tgt->x, tgt->y, ticks});
        }
        break;
    }
    default: break;
    }
}

void World::updateRaiders() {
    float hx = hallX + 1.f, hy = hallY + 3.f;
    for (Raider& r : raiders) {
        if (!r.alive) continue;
        if (r.health <= 0.f) { r.alive = false; say("A raider fell."); continue; }
        Villager* tgt = nullptr; float bd = 1e9f;
        for (Villager& v : villagers) {
            if (!v.alive) continue;
            float d = std::hypot(v.x - r.x, v.y - r.y);
            if (d < bd) { bd = d; tgt = &v; }
        }
        float gx, gy;
        if (r.fleeing) {
            gx = r.x < MAP_W / 2 ? -3.f : MAP_W + 2.f; gy = r.y;
            if (r.x < -2.f || r.x > MAP_W + 1.f) { r.alive = false; continue; }
        } else if (tgt && bd < 4.f) { gx = tgt->x; gy = tgt->y; }
        else { gx = hx; gy = hy; }
        float dx = gx - r.x, dy = gy - r.y, d = std::hypot(dx, dy);
        if (d > 0.6f) { r.x += dx / d * 0.07f; r.y += dy / d * 0.07f; }
        if (!r.fleeing && tgt && bd < 0.9f) {
            int defenders = 0;
            for (const Villager& v : villagers)
                if (v.alive && std::hypot(v.x - r.x, v.y - r.y) < 2.5f) defenders++;
            tgt->health -= 1.2f;
            r.health -= 0.4f * defenders;
            if (tgt->health <= 0.f) kill(*tgt, "killed by raiders");
        }
        if (!r.fleeing && std::hypot(r.x - hx, r.y - hy) < 1.5f) {
            float t = std::min(store[(int)Res::Bread], 0.6f); store[(int)Res::Bread] -= t; r.loot += t;
            if (t < 0.6f) { float f = std::min(store[(int)Res::Fish], 0.6f - t); store[(int)Res::Fish] -= f; r.loot += f; }
            if (r.loot >= 15.f || food() <= 0.f) r.fleeing = true;
        }
    }
    arrows.erase(std::remove_if(arrows.begin(), arrows.end(), [&](const Arrow& a) { return ticks - a.born > 5; }), arrows.end());
}

void World::updateTiles() {
    Season s = season();
    float grow = 0.f;
    if (droughtDays <= 0) {
        if (s == Season::Spring || s == Season::Summer) grow = 1.f / (TICKS_PER_DAY * 2.f);
        else if (s == Season::Autumn) grow = 1.f / (TICKS_PER_DAY * 3.f);
    }
    for (Building& b : buildings) if (b.alive && b.type == BType::Farm) b.grow = std::min(1.f, b.grow + grow);

    float spread = droughtDays > 0 ? 0.012f : (s == Season::Winter ? 0.0015f : 0.005f);
    std::vector<int> ignite;
    std::vector<int> burnt;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            int i = y * MAP_W + x;
            Cell& c = cells[i];
            if (c.t == Tile::Ash) {
                c.res -= 1.f / TICKS_PER_DAY;
                if (c.res <= 0.f) { c.t = Tile::Grass; c.res = 0.f; }
            }
            if (c.t == Tile::Forest && c.res < 3.f && s != Season::Winter) c.res = std::min(3.f, c.res + 0.002f);
            if (c.t == Tile::Grass && s == Season::Spring && frand() < 0.00005f)
                for (int k = 0; k < 4; k++) {
                    int nx = x + DX[k], ny = y + DY[k];
                    if (inside(nx, ny) && at(nx, ny).t == Tile::Forest) { c.t = Tile::Forest; c.res = 1.f; break; }
                }
            if (c.fire > 0.f) {
                bool covered = wellCover[i];
                c.fire -= covered ? 6.f : 1.f;
                if (!covered)
                    for (int k = 0; k < 4; k++) {
                        int nx = x + DX[k], ny = y + DY[k];
                        if (!inside(nx, ny)) continue;
                        int ni = ny * MAP_W + nx;
                        Cell& n = cells[ni];
                        bool fl = n.t == Tile::Forest || n.t == Tile::Grass ||
                                  (n.t == Tile::Building && n.bld > 0);
                        float p = n.t == Tile::Grass ? spread * 0.25f : (n.t == Tile::Building ? spread * 1.5f : spread);
                        if (fl && n.fire <= 0.f && !wellCover[ni] && frand() < p) ignite.push_back(ni);
                    }
                if (c.fire <= 0.f) {
                    c.fire = 0.f;
                    if (covered) continue;   // put out in time
                    if (c.t == Tile::Building) burnt.push_back(c.bld);
                    else if (c.t == Tile::Forest || c.t == Tile::Grass) { c.t = Tile::Ash; c.res = 3.f; }
                }
            }
        }
    for (int i : ignite) if (cells[i].fire <= 0.f) cells[i].fire = 60.f + frand(0.f, 40.f);
    for (int bi : burnt) destroyBuilding(bi, "burned down");
}

void World::startEvent(Event e) {
    lastEvent = e; lastEventDay = day();
    switch (e) {
    case Event::Drought:
        droughtDays = irand(3, 6) + dangerLevel() / 2;
        say("DROUGHT! Crops stop growing and fires spread fast.");
        break;
    case Event::Plague: {
        int n = 1 + day() / 14, done = 0;
        for (int tries = 0; tries < 50 && done < n && !villagers.empty(); tries++) {
            Villager& v = villagers[irand(0, (int)villagers.size() - 1)];
            if (v.alive && !v.sick) { v.sick = true; done++; }
        }
        say("PLAGUE! Villagers are falling sick. A healer helps.");
        break;
    }
    case Event::Wildfire: {
        int n = irand(1 + dangerLevel() / 3, 2 + day() / 15), lit = 0;
        for (int tries = 0; tries < 400 && lit < n; tries++) {
            int x = hallX + irand(-22, 24), y = hallY + irand(-30, 32);
            if (!inside(x, y)) continue;
            Cell& c = at(x, y);
            if (c.t == Tile::Forest && c.fire <= 0.f) { c.fire = 80.f; lit++; }
        }
        if (lit) say("WILDFIRE! Wells protect buildings in their circle.");
        break;
    }
    case Event::Raiders: {
        int n = 2 + day() / 7 + std::max(0, day() - 40) / 5;
        bool left = pendingSide < 0;
        for (int i = 0; i < n; i++) {
            Raider r;
            r.x = left ? 0.f : (float)(MAP_W - 1);
            r.y = std::clamp(hallY + frand(-10.f, 10.f), 0.f, (float)(MAP_H - 1));
            raiders.push_back(r);
        }
        say("RAIDERS! " + std::to_string(n) + " from the " + (left ? "west." : "east.") + " Watchtowers help.");
        break;
    }
    case Event::Locusts: {
        int hit = 0;
        for (Building& b : buildings) if (b.alive && b.type == BType::Farm && frand() < 0.7f) { b.grow = 0.f; hit++; }
        if (hit) say("LOCUSTS! " + std::to_string(hit) + " farms were stripped.");
        break;
    }
    case Event::Blizzard:
        blizzardDays = irand(2, 4) + dangerLevel() / 3;
        say("BLIZZARD! Houses burn double firewood.");
        break;
    default: break;
    }
}

void World::rollEvent() {
    if (!disastersOn || day() < 4 || pending != Event::None) return;
    if (frand() >= std::min(0.9f, 0.15f + day() * 0.011f)) return;
    rollEventNow();
}

// Picks a disaster for this season and announces it; it hits WARN_TICKS later.
void World::rollEventNow() {
    Season s = season();
    struct W { Event e; float w; };
    std::vector<W> opts;
    if (s != Season::Winter && droughtDays <= 0) opts.push_back({Event::Drought, s == Season::Summer ? 2.5f : 1.f});
    if (population() >= 3) opts.push_back({Event::Plague, 1.2f});
    opts.push_back({Event::Wildfire, s == Season::Summer ? 2.5f : (s == Season::Winter ? 0.2f : 1.f)});
    if (day() >= 6) opts.push_back({Event::Raiders, 1.f + day() * 0.03f});
    if (s != Season::Winter && count(BType::Farm) > 0) opts.push_back({Event::Locusts, 0.7f});
    if (s == Season::Winter && blizzardDays <= 0) opts.push_back({Event::Blizzard, 2.f});
    float tot = 0.f; for (auto& o : opts) tot += o.w;
    float r = frand(0.f, tot);
    for (auto& o : opts) if ((r -= o.w) <= 0.f) {
        pending = o.e; pendingTicks = WARN_TICKS;
        pendingSide = o.e == Event::Raiders ? (frand() < 0.5f ? -1 : 1) : 0;
        return;
    }
}

void World::dawn() {
    Season s = season();
    if (day() % DAYS_PER_SEASON == 0 && day() > 0) say(std::string(seasonName(s)) + " begins.");
    if (s == Season::Spring && day() % DAYS_PER_SEASON == 0 && day() > 0 && population() > 0)
        history.push_back({day(), "Survived winter " + std::to_string(day() / (DAYS_PER_SEASON * 4)) + "."});
    int pop = population();
    // eat: bread first, then fish
    float need = (float)pop;
    float b = std::min(need, store[(int)Res::Bread]); store[(int)Res::Bread] -= b; need -= b;
    float f = std::min(need, store[(int)Res::Fish]); store[(int)Res::Fish] -= f; need -= f;
    bool hungry = need > 0.5f;
    if (hungry) {
        int n = (int)std::ceil(need);
        say("Not enough food! " + std::to_string(n) + " went hungry.");
        for (Villager& v : villagers) if (v.alive && n-- > 0) v.health -= 30.f;
    } else for (Villager& v : villagers) if (v.alive && !v.sick) v.health = std::min(100.f, v.health + 10.f);
    // heat
    bool cold = false;
    if (s == Season::Winter) {
        float heat = 1.f;
        for (const Building& bd : buildings) if (bd.alive && bd.type == BType::House && bd.connected) heat += 0.6f;
        if (blizzardDays > 0) heat *= 2.f;
        if (store[(int)Res::Logs] >= heat) store[(int)Res::Logs] -= heat;
        else {
            store[(int)Res::Logs] = 0.f; cold = true;
            say("Not enough firewood! The town is freezing.");
            for (Villager& v : villagers) if (v.alive) v.health -= 15.f;
        }
    }
    for (Building& bd : buildings) if (bd.alive && bd.type == BType::House) { bd.hungry = hungry || food() < pop; bd.cold = cold || (s == Season::Winter && store[(int)Res::Logs] < 5.f); }
    // plague
    for (Villager& v : villagers) {
        if (!v.alive || !v.sick) continue;
        int vx = std::clamp((int)std::lround(v.x), 0, MAP_W - 1), vy = std::clamp((int)std::lround(v.y), 0, MAP_H - 1);
        bool healed = healCover[vy * MAP_W + vx] && count(BType::Healer) > 0;
        bool healerStaffed = false;
        for (const Building& bd : buildings) if (bd.alive && bd.type == BType::Healer && bd.staffed > 0) healerStaffed = true;
        healed = healed && healerStaffed;
        v.health -= healed ? 4.f : 14.f;
        if (frand() < (healed ? 0.6f : 0.15f)) { v.sick = false; continue; }
        if (healed) continue;
        for (Villager& o : villagers)
            if (o.alive && !o.sick && &o != &v && std::hypot(o.x - v.x, o.y - v.y) < 2.5f && frand() < 0.3f) o.sick = true;
    }
    for (Villager& v : villagers) if (v.alive && v.age > v.maxAge) kill(v, "old age");
    for (Villager& v : villagers) if (v.alive && v.health <= 0.f) kill(v, v.sick ? "plague" : "hunger or cold");
    // births
    pop = population();
    int cap = housing();
    if (pop >= 2 && pop < cap && food() >= pop * 4.f && s != Season::Winter) {
        int n = std::min(cap - pop, std::max(1, (cap - 6) / 8 + 1)), born = 0;
        for (int i = 0; i < n; i++)
            if (frand() < 0.6f) { spawnVillager(hallX + 1 + frand(-1.f, 1.f), hallY + 3.2f, 0.f); born++; }
        if (born) { births += born; say(std::to_string(born) + (born == 1 ? " child was born." : " children were born.")); }
    }
    if (droughtDays > 0 && --droughtDays == 0) say("The drought has ended.");
    if (blizzardDays > 0 && --blizzardDays == 0) say("The blizzard has passed.");
    // compact villagers; fix job indices are building indices so they stay valid
    villagers.erase(std::remove_if(villagers.begin(), villagers.end(), [](const Villager& v) { return !v.alive; }), villagers.end());
    raiders.erase(std::remove_if(raiders.begin(), raiders.end(), [](const Raider& r) { return !r.alive; }), raiders.end());
    // town story: big losses yesterday, growth milestones
    if (lostToday >= 2 || diedToday >= 3) {
        Event cause = day() - lastEventDay <= 2 ? lastEvent : Event::None;
        if (lostToday && cause != Event::Wildfire && cause != Event::Drought) cause = Event::Wildfire;   // only fire burns buildings
        // a disaster that keeps hitting for several days becomes one story line
        bool merge = storyIdx == (int)history.size() - 1 && storyIdx >= 0 && storyCause == cause && day() - storyDay <= 2;
        if (!merge) { storyLost = storyDied = 0; history.push_back({day(), ""}); storyIdx = (int)history.size() - 1; }
        storyCause = cause; storyDay = day();
        storyLost += lostToday; storyDied += diedToday;
        std::string t = storyName(cause);
        t += " took ";
        if (storyLost) t += std::to_string(storyLost) + (storyLost == 1 ? " building" : " buildings");
        if (storyLost && storyDied) t += " and ";
        if (storyDied) t += std::to_string(storyDied) + (storyDied == 1 ? " life" : " lives");
        history[storyIdx].text = t + ".";
    }
    lostToday = diedToday = 0;
    for (int m : {10, 25, 50, 100, 200})
        if (popMark < m && population() >= m) { history.push_back({day() + 1, "The town grew to " + std::to_string(m) + " people."}); popMark = m; }
    assignJobs();
    rollEvent();
}

void World::tick() {
    if (over) return;
    if (ticks % TICKS_PER_DAY == 0) dawn();
    // late game: a second disaster can be announced in the afternoon
    if (disastersOn && ticks % TICKS_PER_DAY == TICKS_PER_DAY / 2 && pending == Event::None && day() > 35 &&
        frand() < std::min(0.6f, (day() - 35) * 0.015f)) rollEventNow();
    if (pending != Event::None && --pendingTicks <= 0) { Event e = pending; pending = Event::None; startEvent(e); }
    updateTiles();
    for (int i = 0; i < (int)villagers.size(); i++) if (villagers[i].alive) updateVillager(i);
    updateRaiders();
    if (ticks % 20 == 0) assignJobs();   // pick up newly grown-up or cured villagers
    ticks++;
    if (population() == 0) {
        over = true; pending = Event::None;
        note("The last villager fell.");
        say("The last villager is gone. Your town lasted " + std::to_string(day()) + " days.");
    }
}

} // namespace sim
