#pragma once
// A2DVillageSim core: an Anno-style survival town builder for phones.
// The player places buildings and roads; villagers staff them and run the
// production chains (trees -> logs -> planks, wheat -> flour -> bread, fish).
// Disasters escalate; there are no god powers, only buildings that protect you.
// Score = days survived. No SDL here so the sim runs headless in tests.
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace sim {

constexpr int MAP_W = 64;    // portrait map: fills a phone held upright
constexpr int MAP_H = 120;
constexpr int TICKS_PER_DAY = 240;   // 10 ticks/s at 1x -> 24 s per day
constexpr int DAYS_PER_SEASON = 6;

enum class Tile : uint8_t { Deep, Water, Sand, Grass, Forest, Mountain, Ash, Road, Building };
enum class Season : uint8_t { Spring, Summer, Autumn, Winter };
// Town ages (Empire Earth style): the look of buildings and people advances with what you build.
enum class Age : uint8_t { Camp, Village, Craft, Fortified, Count };
enum class Event : uint8_t { None, Drought, Plague, Wildfire, Raiders, Locusts, Blizzard };
enum class Res : uint8_t { Logs, Planks, Wheat, Flour, Bread, Fish, Count };

enum class BType : uint8_t {
    Hall, House, Lumber, Sawmill, Fisher, Farm, Mill, Bakery, Well, Healer, Tower, Count
};
enum class Category : uint8_t { Home, Food, Wood, Safety, Count };

struct BInfo {
    const char* name;
    int w, h;
    int costLogs, costPlanks;
    int workers;
    float radius;          // coverage (well/healer/tower) or work area (lumber/fisher), tiles
    Category cat;
    const char* desc;
};
const BInfo& binfo(BType t);
const char* resName(Res r);
const char* seasonName(Season s);
const char* ageName(Age a);
const char* categoryName(Category c);

struct Cell {
    Tile t = Tile::Grass;
    float res = 0.f;      // forest wood left, ash regrow timer
    float fire = 0.f;     // >0 burning
    float height = 0.f;
    int16_t bld = -1;     // building index when t == Building
};

struct Building {
    BType type = BType::House;
    int x = 0, y = 0;         // top-left tile
    bool alive = true;
    bool connected = false;   // road link to the hall
    int staffed = 0;          // workers currently assigned
    float work = 0.f;         // production progress
    float grow = 0.f;         // farm crop growth 0..1
    float cooldown = 0.f;     // tower reload
    bool hungry = false, cold = false;   // houses: missing needs (shown as icons)
};

struct Villager {
    float x = 0, y = 0;   // tile coords (float)
    float health = 100.f;
    float age = 0.f;      // days
    float maxAge = 70.f;
    float timer = 0.f;
    bool sick = false;
    bool alive = true;
    int work = -1;        // building index or -1
    int task = 0;
    int tx = -1, ty = -1;
    bool carrying = false;
    std::vector<int> path;
    size_t pathPos = 0;
};

struct Raider {
    float x = 0, y = 0;
    float health = 60.f;
    bool alive = true;
    bool fleeing = false;
    float loot = 0.f;
};

struct Arrow { float x0, y0, x1, y1; int born; };
struct LogLine { int day; std::string text; };
constexpr int WARN_TICKS = 80;   // a disaster is announced this long (8 s at 1x) before it hits

class World {
public:
    explicit World(uint32_t seed = 1, bool disasters = true);

    void tick();   // one fixed step (0.1 s at 1x)

    // Building API (used by the UI and by tests)
    bool canAfford(BType t) const;
    bool canPlace(BType t, int x, int y) const;   // fits on free land
    bool place(BType t, int x, int y);            // pays and builds instantly
    bool canRoad(int x, int y) const;
    bool placeRoad(int x, int y);                 // 1 log per tile
    bool demolish(int x, int y);                  // building or road, refunds half
    int buildingAt(int x, int y) const;

    Cell& at(int x, int y) { return cells[y * MAP_W + x]; }
    const Cell& at(int x, int y) const { return cells[y * MAP_W + x]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }

    int day() const { return ticks / TICKS_PER_DAY; }
    float dayFrac() const { return (ticks % TICKS_PER_DAY) / (float)TICKS_PER_DAY; }
    Season season() const { return (Season)((day() / DAYS_PER_SEASON) % 4); }
    Age age() const { return currentAge; }   // only advances when the player buys it
    Age nextAge() const;
    bool canAdvance() const;                  // prereqs + stock
    void advanceCost(int& logs, int& planks, int& bread) const;
    bool tryAdvance();                        // pay, restyle, start ceremony
    int ageCeremony = 0;                      // ticks left of the advance flash
    int population() const;
    int housing() const;
    int sickCount() const;
    int raidersAlive() const;
    int count(BType t) const;
    int jobsOpen() const;
    float food() const { return store[(int)Res::Bread] + store[(int)Res::Fish]; }
    bool gameOver() const { return over; }
    // Upcoming disaster (announced, not hit yet). pendingSide: -1 west, +1 east, 0 everywhere.
    Event pending = Event::None; int pendingTicks = 0; int pendingSide = 0;
    static const char* warnText(Event e, int side);
    int dangerLevel() const;   // 0.. grows with the day count; more and bigger disasters
    // Town story for the game-over screen: founding, firsts, growth, big losses, the end.
    std::vector<LogLine> history;

    std::vector<Cell> cells;
    std::vector<Building> buildings;
    std::vector<Villager> villagers;
    std::vector<Raider> raiders;
    std::vector<Arrow> arrows;
    std::vector<LogLine> log;
    float store[(int)Res::Count] = {};
    int hallX = 0, hallY = 0;      // hall top-left
    int droughtDays = 0, blizzardDays = 0;
    int ticks = 0;
    bool disastersOn = true;
    bool over = false;
    int births = 0, deaths = 0;
    int dirty = 1;                 // bumps when buildings/roads change (UI can watch it)

private:
    std::mt19937 rng;
    std::vector<uint8_t> wellCover, healCover;

    float frand(float a = 0.f, float b = 1.f);
    int irand(int a, int b);
    void generate();
    void refresh();          // connectivity, coverage, staffing
    void assignJobs();
    void dawn();
    void rollEvent();
    void rollEventNow();
    void startEvent(Event e);
    void note(const std::string& s) { history.push_back({day() + 1, s}); }
    Event lastEvent = Event::None; int lastEventDay = -100;
    int lostToday = 0, diedToday = 0, popMark = 0;
    Event storyCause = Event::None; int storyDay = -100, storyIdx = -1, storyLost = 0, storyDied = 0;
    bool built[(int)BType::Count] = {};
    Age currentAge = Age::Camp;
    void updateVillager(int vi);
    void updateBuilding(int bi);
    void updateRaiders();
    void updateTiles();
    void destroyBuilding(int bi, const char* why);
    bool nearestTile(int sx, int sy, Tile t, int r, int& ox, int& oy) const;
    void spawnVillager(float x, float y, float age);
    void kill(Villager& v, const char* why);
    void say(const std::string& s);
    bool take(Res r, float n);
public:
    void doorOf(const Building& b, int& x, int& y) const;   // a walkable tile next to it
};

} // namespace sim
