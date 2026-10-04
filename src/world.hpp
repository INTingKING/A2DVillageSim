#pragma once
// A2DVillageSim core: a god-view survival sim. Villagers run themselves;
// disasters escalate; the player spends mana on divine powers. Score = days survived.
// No SDL here so the sim can run headless in tests.
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace sim {

constexpr int MAP_W = 64;    // portrait map: fills a phone held upright
constexpr int MAP_H = 120;
constexpr int TICKS_PER_DAY = 240;   // 10 ticks/s at 1x -> 24 s per day
constexpr int DAYS_PER_SEASON = 6;

enum class Tile : uint8_t { Deep, Water, Sand, Grass, Forest, Mountain, Farm, House, Hall, Ash };
enum class Season : uint8_t { Spring, Summer, Autumn, Winter };
enum class Job : uint8_t { Farmer, Lumber, Builder };
enum class Power : uint8_t { Rain, Heal, Bless, Smite, Count };
enum class Event : uint8_t { None, Drought, Plague, Wildfire, Raiders, Locusts, Blizzard };

struct Cell {
    Tile t = Tile::Grass;
    float res = 0.f;      // forest wood left, farm growth 0..1, ash regrow timer
    float fire = 0.f;     // >0 burning
    float height = 0.f;
};

struct Villager {
    float x = 0, y = 0;   // tile coords (float)
    int tx = -1, ty = -1; // target tile
    Job job = Job::Farmer;
    float health = 100.f;
    float age = 0.f;      // days
    float maxAge = 70.f;
    float workTimer = 0.f;
    bool sick = false;
    bool carrying = false;
    bool alive = true;
    uint32_t id = 0;
    int task = 0;              // see World::Task
    float carryAmt = 0.f;
    uint8_t carryKind = 0;     // 0 food, 1 wood
    std::vector<int> path;     // tile indices to walk
    size_t pathPos = 0;
};

struct Raider {
    float x = 0, y = 0;
    float health = 60.f;
    bool alive = true;
    bool fleeing = false;
    float loot = 0.f;
};

struct LogLine { int day; std::string text; };

struct PowerInfo { const char* name; int cost; float radius; const char* desc; };
const PowerInfo& powerInfo(Power p);
const char* seasonName(Season s);

class World {
public:
    explicit World(uint32_t seed = 1, bool disasters = true);

    void tick();                       // one fixed step (0.1 s at 1x)
    bool cast(Power p, int cx, int cy); // false if not enough mana / invalid

    Cell& at(int x, int y) { return cells[y * MAP_W + x]; }
    const Cell& at(int x, int y) const { return cells[y * MAP_W + x]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }

    int day() const { return ticks / TICKS_PER_DAY; }
    float dayFrac() const { return (ticks % TICKS_PER_DAY) / (float)TICKS_PER_DAY; }
    Season season() const { return (Season)((day() / DAYS_PER_SEASON) % 4); }
    int population() const;
    int houses() const { return houseCount; }
    int farms() const { return farmCount; }
    int sickCount() const;
    int fireCount() const;
    int raidersAlive() const;
    bool gameOver() const { return over; }

    std::vector<Cell> cells;
    std::vector<Villager> villagers;
    std::vector<Raider> raiders;
    std::vector<LogLine> log;
    float food = 40.f, wood = 30.f, mana = 60.f;
    int hallX = 0, hallY = 0;
    int droughtDays = 0, blizzardDays = 0;
    int ticks = 0;
    bool disastersOn = true;
    bool over = false;
    int births = 0, deaths = 0;
    std::string causeOfEnd;
    // last cast, for visual effects
    Power lastPower = Power::Rain;
    int lastCastX = -1, lastCastY = -1, lastCastTick = -1000;

private:
    std::mt19937 rng;
    uint32_t nextId = 1;
    int houseCount = 0, farmCount = 0;

    float frand(float a = 0.f, float b = 1.f);
    int irand(int a, int b);
    void generate();
    void recount();
    void dawn();
    void rollEvent();
    void startEvent(Event e);
    void updateVillager(Villager& v);
    void updateRaiders();
    void updateTiles();
    bool findNearest(int sx, int sy, Tile t, int& ox, int& oy, float minRes, int maxR) const;
    bool findBuildSpot(int& ox, int& oy);
    void spawnVillager(float x, float y, float age);
    void kill(Villager& v, const char* why);
    void say(const std::string& s);
};

} // namespace sim
