// Headless checks for the sim core. Run: ./build/villagesim_test
#include "world.hpp"
#include <cstdio>

using namespace sim;

static int runDays(World& w, int days) {
    for (int t = 0; t < days * TICKS_PER_DAY && !w.gameOver(); t++) w.tick();
    return w.day();
}

int main() {
    bool ok = true;
    // 1. Peaceful world: the village should grow on its own.
    {
        World w(7, false);
        int p0 = w.population(), h0 = w.houses();
        runDays(w, 40);
        std::printf("peaceful 40d: pop %d -> %d, houses %d -> %d, farms %d, food %.0f wood %.0f\n",
                    p0, w.population(), h0, w.houses(), w.farms(), w.food, w.wood);
        if (w.population() <= p0 || w.houses() <= h0) { std::printf("  FAIL: village did not grow\n"); ok = false; }
    }
    // 2. Disasters with no god: civilization should eventually fall, but not instantly.
    {
        int total = 0, minD = 1 << 30, maxD = 0;
        for (uint32_t seed = 1; seed <= 8; seed++) {
            World w(seed, true);
            int d = runDays(w, 400);
            total += d; minD = std::min(minD, d); maxD = std::max(maxD, d);
            std::printf("  seed %u: %s at day %d (pop %d)\n", seed, w.gameOver() ? "fell" : "alive", d, w.population());
        }
        std::printf("no-god survival: avg %.1f days (min %d, max %d)\n", total / 8.0, minD, maxD);
        if (minD < 6) { std::printf("  FAIL: collapses too fast\n"); ok = false; }
    }
    // 3. Powers do what they say.
    {
        World w(3, false);
        Raider r; r.x = (float)w.hallX + 3; r.y = (float)w.hallY; w.raiders.push_back(r);
        w.mana = 100;
        bool cast = w.cast(Power::Smite, w.hallX + 3, w.hallY);
        std::printf("smite: cast=%d raiders alive=%d\n", cast, w.raidersAlive());
        if (!cast || w.raidersAlive() != 0) { std::printf("  FAIL smite\n"); ok = false; }
        w.at(w.hallX + 4, w.hallY + 4).fire = 50; w.mana = 100;
        w.cast(Power::Rain, w.hallX + 4, w.hallY + 4);
        if (w.at(w.hallX + 4, w.hallY + 4).fire > 0) { std::printf("  FAIL rain\n"); ok = false; }
        w.mana = 0;
        if (w.cast(Power::Heal, w.hallX, w.hallY)) { std::printf("  FAIL: cast without mana\n"); ok = false; }
    }
    std::printf("%s\n", ok ? "ALL OK" : "FAILED");
    return ok ? 0 : 1;
}
