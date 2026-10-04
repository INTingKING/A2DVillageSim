#include "world.hpp"
#include <algorithm>
#include <cmath>
#include <queue>

namespace sim {

namespace {
enum Task { T_IDLE = 0, T_HARVEST, T_PLANT, T_CHOP, T_BUILD, T_RETURN, T_WANDER };

const PowerInfo kPowers[(int)Power::Count] = {
    {"Rain",  30, 7.f, "puts out fires, ends drought, waters farms"},
    {"Heal",  25, 5.f, "cures plague, restores health"},
    {"Bless", 35, 6.f, "ripens every farm in the circle"},
    {"Smite", 15, 2.f, "lightning: kills raiders (and villagers), sparks fire"},
};

bool walkable(Tile t) {
    return t == Tile::Sand || t == Tile::Grass || t == Tile::Forest || t == Tile::Farm ||
           t == Tile::House || t == Tile::Hall || t == Tile::Ash;
}
bool flammable(Tile t) {
    return t == Tile::Forest || t == Tile::Farm || t == Tile::House || t == Tile::Grass;
}

// value noise
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
} // namespace

const PowerInfo& powerInfo(Power p) { return kPowers[(int)p]; }
const char* seasonName(Season s) {
    static const char* n[] = {"Spring", "Summer", "Autumn", "Winter"};
    return n[(int)s];
}

World::World(uint32_t seed, bool disasters) : disastersOn(disasters), rng(seed) {
    generate();
}

float World::frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
int World::irand(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

void World::say(const std::string& s) {
    log.push_back({day() + 1, s});
    if (log.size() > 60) log.erase(log.begin());
}

void World::generate() {
    cells.assign(MAP_W * MAP_H, Cell{});
    uint32_t seed = rng();
    for (int y = 0; y < MAP_H; y++) {
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
    }
    // Town hall: grass tile nearest centre with lots of land around.
    int best = -1; float bestScore = -1e9f;
    for (int y = 8; y < MAP_H - 8; y++)
        for (int x = 10; x < MAP_W - 10; x++) {
            if (at(x, y).t != Tile::Grass) continue;
            int land = 0;
            for (int dy = -5; dy <= 5; dy++)
                for (int dx = -5; dx <= 5; dx++) {
                    Tile t = at(x + dx, y + dy).t;
                    if (t == Tile::Grass) land += 2; else if (walkable(t)) land += 1;
                }
            float d = std::hypot(x - MAP_W * 0.5f, y - MAP_H * 0.5f);
            float sc = land - d * 0.8f;
            if (sc > bestScore) { bestScore = sc; best = y * MAP_W + x; }
        }
    if (best < 0) best = (MAP_H / 2) * MAP_W + MAP_W / 2;
    hallX = best % MAP_W; hallY = best / MAP_W;
    at(hallX, hallY).t = Tile::Hall;
    // clear a little plaza, two houses, three farms
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            Cell& c = at(hallX + dx, hallY + dy);
            if (c.t == Tile::Forest || c.t == Tile::Mountain || c.t == Tile::Water || c.t == Tile::Deep) { c.t = Tile::Grass; c.res = 0; }
        }
    const int hp[2][2] = {{-2, -1}, {2, -1}};
    for (auto& h : hp) { Cell& c = at(hallX + h[0], hallY + h[1]); c.t = Tile::House; c.res = 0; }
    for (int i = 0; i < 3; i++) { Cell& c = at(hallX - 1 + i, hallY + 2); c.t = Tile::Farm; c.res = 0.6f + 0.15f * i; }
    recount();
    for (int i = 0; i < 6; i++) spawnVillager(hallX + frand(-1.f, 1.f), hallY + frand(-1.f, 1.f), frand(16.f, 30.f));
    say("A small village is founded. Keep it alive.");
}

void World::recount() {
    houseCount = farmCount = 0;
    for (const Cell& c : cells) {
        if (c.t == Tile::House) houseCount++;
        else if (c.t == Tile::Farm) farmCount++;
    }
}

int World::population() const {
    int n = 0; for (const auto& v : villagers) n += v.alive; return n;
}
int World::sickCount() const {
    int n = 0; for (const auto& v : villagers) n += (v.alive && v.sick); return n;
}
int World::fireCount() const {
    int n = 0; for (const auto& c : cells) n += c.fire > 0.f; return n;
}
int World::raidersAlive() const {
    int n = 0; for (const auto& r : raiders) n += r.alive; return n;
}

void World::spawnVillager(float x, float y, float age) {
    Villager v;
    v.x = x; v.y = y; v.age = age;
    v.maxAge = frand(55.f, 80.f);
    v.id = nextId++;
    villagers.push_back(v);
}

void World::kill(Villager& v, const char* why) {
    if (!v.alive) return;
    v.alive = false;
    deaths++;
    say(std::string("A villager died: ") + why + ".");
}

// BFS from (sx,sy) over walkable tiles to the nearest tile of type t with res >= minRes.
bool World::findNearest(int sx, int sy, Tile t, int& ox, int& oy, float minRes, int maxR) const {
    if (!inside(sx, sy)) return false;
    std::vector<uint8_t> seen(MAP_W * MAP_H, 0);
    std::queue<int> q;
    q.push(sy * MAP_W + sx); seen[sy * MAP_W + sx] = 1;
    while (!q.empty()) {
        int i = q.front(); q.pop();
        int x = i % MAP_W, y = i / MAP_W;
        if (std::abs(x - sx) > maxR || std::abs(y - sy) > maxR) continue;
        const Cell& c = cells[i];
        if (c.t == t && c.res >= minRes && c.fire <= 0.f) { ox = x; oy = y; return true; }
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!inside(nx, ny)) continue;
            int ni = ny * MAP_W + nx;
            if (seen[ni] || !walkable(cells[ni].t)) continue;
            seen[ni] = 1; q.push(ni);
        }
    }
    return false;
}

// Grass tile near the hall, preferring spots next to existing buildings.
bool World::findBuildSpot(int& ox, int& oy) {
    float best = -1e9f; int bx = -1, by = -1;
    for (int y = std::max(1, hallY - 10); y < std::min(MAP_H - 1, hallY + 11); y++)
        for (int x = std::max(1, hallX - 14); x < std::min(MAP_W - 1, hallX + 15); x++) {
            if (at(x, y).t != Tile::Grass || at(x, y).fire > 0.f) continue;
            int adj = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    Tile t = at(x + dx, y + dy).t;
                    if (t == Tile::House || t == Tile::Hall || t == Tile::Farm) adj++;
                }
            float d = std::hypot((float)(x - hallX), (float)(y - hallY));
            float sc = adj * 2.f - d + frand(0.f, 1.5f);
            if (sc > best) { best = sc; bx = x; by = y; }
        }
    if (bx < 0) return false;
    ox = bx; oy = by; return true;
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
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!w.inside(nx, ny)) continue;
            int ni = ny * MAP_W + nx;
            if (prev[ni] != -2) continue;
            Tile tt = w.at(nx, ny).t;
            bool ok = tt == Tile::Sand || tt == Tile::Grass || tt == Tile::Forest || tt == Tile::Farm ||
                      tt == Tile::House || tt == Tile::Hall || tt == Tile::Ash;
            if (!ok && ni != t) continue;
            prev[ni] = i; q.push(ni);
        }
    }
    std::vector<int> path;
    if (prev[t] == -2) return path;
    for (int i = t; i != -1; i = prev[i]) path.push_back(i);
    std::reverse(path.begin(), path.end());
    return path;
}

void World::updateVillager(Villager& v) {
    v.age += 1.f / TICKS_PER_DAY;
    int vx = std::clamp((int)std::lround(v.x), 0, MAP_W - 1);
    int vy = std::clamp((int)std::lround(v.y), 0, MAP_H - 1);
    if (at(vx, vy).fire > 0.f) v.health -= 1.5f;
    if (v.health <= 0.f) { kill(v, at(vx, vy).fire > 0.f ? "burned" : (v.sick ? "plague" : "starvation or cold")); return; }

    auto goTo = [&](int tx, int ty, int task) {
        v.path = bfsPath(*this, vx, vy, tx, ty);
        v.pathPos = 0; v.tx = tx; v.ty = ty; v.task = task; v.workTimer = 0.f;
        return !v.path.empty();
    };

    // walking
    if (v.pathPos < v.path.size()) {
        int ni = v.path[v.pathPos];
        float gx = (float)(ni % MAP_W), gy = (float)(ni / MAP_W);
        float dx = gx - v.x, dy = gy - v.y, d = std::hypot(dx, dy);
        float sp = v.sick ? 0.05f : 0.09f;
        if (d <= sp) { v.x = gx; v.y = gy; v.pathPos++; }
        else { v.x += dx / d * sp; v.y += dy / d * sp; }
        return;
    }

    // arrived (or idle)
    switch (v.task) {
    case T_RETURN:
        if (v.carryKind == 0) food += v.carryAmt; else wood += v.carryAmt;
        v.carryAmt = 0.f; v.carrying = false; v.task = T_IDLE;
        return;
    case T_HARVEST: {
        Cell& c = at(v.tx, v.ty);
        if (c.t != Tile::Farm || c.res < 0.95f) { v.task = T_IDLE; return; }
        if ((v.workTimer += 1.f) < 15.f) return;
        c.res = 0.f;
        v.carrying = true; v.carryKind = 0; v.carryAmt = 6.f;
        goTo(hallX, hallY, T_RETURN);
        return;
    }
    case T_PLANT: {
        Cell& c = at(v.tx, v.ty);
        if (c.t != Tile::Grass) { v.task = T_IDLE; return; }
        if ((v.workTimer += 1.f) < 25.f) return;
        c.t = Tile::Farm; c.res = 0.f; farmCount++;
        v.task = T_IDLE;
        return;
    }
    case T_CHOP: {
        Cell& c = at(v.tx, v.ty);
        if (c.t != Tile::Forest || c.res <= 0.f) { v.task = T_IDLE; return; }
        if ((v.workTimer += 1.f) < 22.f) return;
        c.res -= 1.f;
        if (c.res <= 0.f) { c.t = Tile::Grass; c.res = 0.f; }
        v.carrying = true; v.carryKind = 1; v.carryAmt = 3.f;
        goTo(hallX, hallY, T_RETURN);
        return;
    }
    case T_BUILD: {
        Cell& c = at(v.tx, v.ty);
        if (c.t != Tile::Grass) { wood += 12.f; v.task = T_IDLE; return; }
        if ((v.workTimer += 1.f) < 50.f) return;
        c.t = Tile::House; c.res = 0.f; houseCount++;
        say("A new house was built.");
        v.task = T_IDLE;
        return;
    }
    case T_WANDER:
        if ((v.workTimer += 1.f) < 20.f) return;
        v.task = T_IDLE;
        return;
    default: break;
    }

    // choose next task
    int tx, ty;
    int pop = population();
    if (v.job == Job::Builder && wood >= 12.f && pop + 2 >= houseCount * 4 && findBuildSpot(tx, ty)) {
        wood -= 12.f;
        if (goTo(tx, ty, T_BUILD)) return;
        wood += 12.f;
    }
    if (v.job == Job::Farmer) {
        if (findNearest(vx, vy, Tile::Farm, tx, ty, 0.95f, 30) && goTo(tx, ty, T_HARVEST)) return;
        int want = std::max(4, (int)std::ceil(pop * 0.8f));
        if (farmCount < want && season() != Season::Winter && findBuildSpot(tx, ty) && goTo(tx, ty, T_PLANT)) return;
    }
    if (v.job != Job::Farmer || frand() < 0.3f) {
        if (findNearest(vx, vy, Tile::Forest, tx, ty, 0.5f, 40) && goTo(tx, ty, T_CHOP)) return;
    }
    // wander near the hall
    int wx = std::clamp(hallX + irand(-4, 4), 0, MAP_W - 1);
    int wy = std::clamp(hallY + irand(-3, 3), 0, MAP_H - 1);
    if (!walkable(at(wx, wy).t) || !goTo(wx, wy, T_WANDER)) v.task = T_WANDER;
}

void World::updateRaiders() {
    for (Raider& r : raiders) {
        if (!r.alive) continue;
        if (r.health <= 0.f) { r.alive = false; say("A raider fell."); continue; }
        // nearest villager
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
        else { gx = (float)hallX; gy = (float)hallY; }
        float dx = gx - r.x, dy = gy - r.y, d = std::hypot(dx, dy);
        if (d > 0.6f) { r.x += dx / d * 0.07f; r.y += dy / d * 0.07f; }
        if (!r.fleeing && tgt && bd < 0.9f) {
            int defenders = 0;
            for (const Villager& v : villagers)
                if (v.alive && std::hypot(v.x - r.x, v.y - r.y) < 2.5f) defenders++;
            tgt->health -= 1.2f;
            r.health -= 0.45f * defenders;
            if (tgt->health <= 0.f) kill(*tgt, "killed by raiders");
        }
        if (!r.fleeing && std::hypot(r.x - hallX, r.y - hallY) < 1.2f) {
            float take = std::min(food, 0.6f);
            food -= take; r.loot += take;
            if (r.loot >= 12.f || food <= 0.f) r.fleeing = true;
        }
    }
}

void World::updateTiles() {
    Season s = season();
    float grow = 0.f;
    if (droughtDays <= 0) {
        if (s == Season::Spring || s == Season::Summer) grow = 1.f / (TICKS_PER_DAY * 2.f);
        else if (s == Season::Autumn) grow = 1.f / (TICKS_PER_DAY * 3.5f);
    }
    float spread = droughtDays > 0 ? 0.012f : (s == Season::Winter ? 0.0015f : 0.005f);
    std::vector<int> ignite;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Cell& c = at(x, y);
            if (c.t == Tile::Farm && c.fire <= 0.f) c.res = std::min(1.f, c.res + grow);
            if (c.t == Tile::Ash) {
                c.res -= 1.f / TICKS_PER_DAY;
                if (c.res <= 0.f) { c.t = Tile::Grass; c.res = 0.f; }
            }
            if (c.t == Tile::Forest && c.res < 3.f && s != Season::Winter) c.res = std::min(3.f, c.res + 0.0015f);
            if (c.t == Tile::Grass && s == Season::Spring && frand() < 0.00004f) {
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (inside(nx, ny) && at(nx, ny).t == Tile::Forest) { c.t = Tile::Forest; c.res = 0.5f; break; }
                }
            }
            if (c.fire > 0.f) {
                c.fire -= 1.f;
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (!inside(nx, ny)) continue;
                    Cell& n = at(nx, ny);
                    float p = n.t == Tile::Grass ? spread * 0.25f : spread;
                    if (n.fire <= 0.f && flammable(n.t) && frand() < p) ignite.push_back(ny * MAP_W + nx);
                }
                if (c.fire <= 0.f) {
                    if (c.t == Tile::House) say("A house burned down.");
                    c.t = Tile::Ash; c.res = 3.f; c.fire = 0.f;
                }
            }
        }
    for (int i : ignite) if (cells[i].fire <= 0.f) cells[i].fire = 60.f + frand(0.f, 40.f);
    if (!ignite.empty()) recount();
}

void World::startEvent(Event e) {
    switch (e) {
    case Event::Drought:
        droughtDays = irand(3, 7);
        say("DROUGHT! Crops stop growing and fires spread fast. (Rain ends it)");
        break;
    case Event::Plague: {
        int n = 1 + day() / 15, done = 0;
        for (int tries = 0; tries < 50 && done < n; tries++) {
            Villager& v = villagers[irand(0, (int)villagers.size() - 1)];
            if (v.alive && !v.sick) { v.sick = true; done++; }
        }
        say("PLAGUE! Villagers are falling sick. (Heal cures them)");
        break;
    }
    case Event::Wildfire: {
        int n = irand(1, 2 + day() / 20), lit = 0;
        for (int tries = 0; tries < 400 && lit < n; tries++) {
            int x = hallX + irand(-25, 25), y = hallY + irand(-18, 18);
            if (!inside(x, y)) continue;
            Cell& c = at(x, y);
            if (c.t == Tile::Forest && c.fire <= 0.f) { c.fire = 80.f; lit++; }
        }
        if (lit) say("WILDFIRE! The forest is burning. (Rain puts it out)");
        break;
    }
    case Event::Raiders: {
        int n = 2 + day() / 8;
        bool left = frand() < 0.5f;
        for (int i = 0; i < n; i++) {
            Raider r;
            r.x = left ? 0.f : (float)(MAP_W - 1);
            r.y = std::clamp(hallY + frand(-8.f, 8.f), 0.f, (float)(MAP_H - 1));
            raiders.push_back(r);
        }
        say("RAIDERS! " + std::to_string(n) + " raiders approach from the " + (left ? "west." : "east.") + " (Smite them)");
        break;
    }
    case Event::Locusts: {
        int hit = 0;
        for (Cell& c : cells) if (c.t == Tile::Farm && frand() < 0.7f) { c.res = 0.f; hit++; }
        say("LOCUSTS! " + std::to_string(hit) + " fields were stripped. (Bless regrows them)");
        break;
    }
    case Event::Blizzard:
        blizzardDays = irand(2, 4);
        food *= 0.9f;
        say("BLIZZARD! Houses need double firewood.");
        break;
    default: break;
    }
}

void World::rollEvent() {
    if (!disastersOn || day() < 3) return;
    float p = std::min(0.8f, 0.18f + day() * 0.012f);
    if (frand() >= p) return;
    Season s = season();
    struct W { Event e; float w; };
    std::vector<W> opts;
    if (s != Season::Winter && droughtDays <= 0) opts.push_back({Event::Drought, s == Season::Summer ? 3.f : 1.5f});
    if (population() >= 3) opts.push_back({Event::Plague, 1.2f});
    opts.push_back({Event::Wildfire, s == Season::Summer ? 2.5f : (s == Season::Winter ? 0.2f : 1.f)});
    if (day() >= 5) opts.push_back({Event::Raiders, 1.f + day() * 0.03f});
    if (s != Season::Winter && farmCount > 0) opts.push_back({Event::Locusts, 0.8f});
    if (s == Season::Winter && blizzardDays <= 0) opts.push_back({Event::Blizzard, 2.f});
    float tot = 0.f; for (auto& o : opts) tot += o.w;
    float r = frand(0.f, tot);
    for (auto& o : opts) { if ((r -= o.w) <= 0.f) { startEvent(o.e); return; } }
}

void World::dawn() {
    int pop = population();
    Season s = season();
    if (day() % DAYS_PER_SEASON == 0 && day() > 0) say(std::string(seasonName(s)) + " begins.");

    // eat
    float need = (float)pop;
    if (food >= need) {
        food -= need;
        for (Villager& v : villagers) if (v.alive && !v.sick) v.health = std::min(100.f, v.health + 10.f);
    } else {
        int hungry = (int)std::ceil(need - food);
        food = 0.f;
        say("Not enough food! " + std::to_string(hungry) + " went hungry.");
        for (Villager& v : villagers) if (v.alive && hungry-- > 0) v.health -= 30.f;
    }
    // heat
    if (s == Season::Winter) {
        float heat = houseCount * 0.6f * (blizzardDays > 0 ? 2.f : 1.f) + 1.f;
        if (wood >= heat) wood -= heat;
        else {
            wood = 0.f;
            say("Not enough firewood! The village is freezing.");
            for (Villager& v : villagers) if (v.alive) v.health -= 15.f;
        }
    }
    // plague
    for (Villager& v : villagers) {
        if (!v.alive || !v.sick) continue;
        v.health -= 14.f;
        if (frand() < 0.18f) v.sick = false;
        for (Villager& o : villagers)
            if (o.alive && !o.sick && &o != &v && std::hypot(o.x - v.x, o.y - v.y) < 2.5f && frand() < 0.3f) o.sick = true;
    }
    // old age
    for (Villager& v : villagers) if (v.alive && v.age > v.maxAge) kill(v, "old age");
    for (Villager& v : villagers) if (v.alive && v.health <= 0.f) kill(v, v.sick ? "plague" : "starvation or cold");

    // births
    pop = population();
    int cap = houseCount * 4 + 2;
    if (pop >= 2 && pop < cap && food >= pop * 4.f && s != Season::Winter) {
        int n = std::min(cap - pop, std::max(1, houseCount / 2));
        int born = 0;
        for (int i = 0; i < n; i++)
            if (frand() < 0.55f) { spawnVillager(hallX + frand(-0.5f, 0.5f), hallY + frand(-0.5f, 0.5f), 0.f); born++; }
        if (born) { births += born; say(std::to_string(born) + (born == 1 ? " child was born." : " children were born.")); }
    }

    // jobs
    pop = population();
    float farmShare = food < pop * 3.f ? 0.65f : 0.45f;
    int farmers = std::max(1, (int)std::ceil(pop * farmShare));
    int builders = pop >= 4 ? 1 : 0;
    int k = 0;
    for (Villager& v : villagers) {
        if (!v.alive) continue;
        v.job = k < farmers ? Job::Farmer : (k < farmers + builders ? Job::Builder : Job::Lumber);
        k++;
    }

    if (droughtDays > 0 && --droughtDays == 0) say("The drought has ended.");
    if (blizzardDays > 0 && --blizzardDays == 0) say("The blizzard has passed.");
    villagers.erase(std::remove_if(villagers.begin(), villagers.end(), [](const Villager& v) { return !v.alive; }), villagers.end());
    raiders.erase(std::remove_if(raiders.begin(), raiders.end(), [](const Raider& r) { return !r.alive; }), raiders.end());
    rollEvent();
}

void World::tick() {
    if (over) return;
    if (ticks % TICKS_PER_DAY == 0) dawn();
    mana = std::min(100.f, mana + 0.045f);
    updateTiles();
    for (Villager& v : villagers) if (v.alive) updateVillager(v);
    updateRaiders();
    ticks++;
    if (population() == 0) {
        over = true;
        causeOfEnd = log.empty() ? "" : log.back().text;
        say("The last villager is gone. Your civilization lasted " + std::to_string(day()) + " days.");
    }
}

bool World::cast(Power p, int cx, int cy) {
    if (over || !inside(cx, cy)) return false;
    const PowerInfo& pi = powerInfo(p);
    if (mana < pi.cost) return false;
    mana -= pi.cost;
    lastPower = p; lastCastX = cx; lastCastY = cy; lastCastTick = ticks;
    float r = pi.radius;
    auto inR = [&](float x, float y) { return std::hypot(x - cx, y - cy) <= r; };
    int ir = (int)std::ceil(r);
    for (int y = cy - ir; y <= cy + ir; y++)
        for (int x = cx - ir; x <= cx + ir; x++) {
            if (!inside(x, y) || !inR((float)x, (float)y)) continue;
            Cell& c = at(x, y);
            if (p == Power::Rain) {
                c.fire = 0.f;
                if (c.t == Tile::Farm && season() != Season::Winter) c.res = std::min(1.f, c.res + 0.5f);
            } else if (p == Power::Bless) {
                if (c.t == Tile::Farm) c.res = 1.f;
            } else if (p == Power::Smite) {
                if (flammable(c.t) && c.t != Tile::Grass && frand() < 0.5f) c.fire = 50.f;
            }
        }
    if (p == Power::Rain && droughtDays > 0) { droughtDays = 0; say("Rain breaks the drought."); }
    for (Villager& v : villagers) {
        if (!v.alive || !inR(v.x, v.y)) continue;
        if (p == Power::Heal) { v.sick = false; v.health = 100.f; }
        if (p == Power::Smite) { v.health -= 80.f; if (v.health <= 0.f) kill(v, "struck by your lightning"); }
    }
    for (Raider& rd : raiders) if (rd.alive && p == Power::Smite && inR(rd.x, rd.y)) { rd.alive = false; say("Lightning strikes a raider down."); }
    return true;
}

} // namespace sim
