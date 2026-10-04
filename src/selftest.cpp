// Headless checks for the sim core. Run: ./build/villagesim_test
// A tiny scripted "player" builds a sensible town so we can test the economy and balance.
#include "autoplay.hpp"
#include <cmath>
#include <cstdio>
#include <queue>

using namespace sim;
using namespace autoplay;

int main() {
    bool ok = true;
    // 1. Chains work: logs -> planks, wheat -> flour -> bread.
    {
        World w(7, false);
        playRun(w, 20, player);
        std::printf("peaceful 20d: pop %d (housing %d), houses %d, farms %d, mills %d, bakeries %d | logs %.0f planks %.0f wheat %.0f flour %.0f bread %.0f fish %.0f\n",
                    w.population(), w.housing(), w.count(BType::House), w.count(BType::Farm), w.count(BType::Mill), w.count(BType::Bakery),
                    w.store[0], w.store[1], w.store[2], w.store[3], w.store[4], w.store[5]);
        int unconnected = 0; for (auto& b : w.buildings) unconnected += b.alive && !b.connected;
        if (unconnected) { std::printf("  FAIL: %d buildings not connected\n", unconnected); ok = false; }
        if (w.population() <= 5) { std::printf("  FAIL: town did not grow\n"); ok = false; }
    }
    // 2. Peaceful long run stays alive.
    {
        World w(11, false);
        int d = playRun(w, 60, player);
        std::printf("peaceful 60d: %s, pop %d\n", w.gameOver() ? "FELL" : "alive", w.population());
        if (w.gameOver() || d < 60) { std::printf("  FAIL: peaceful town died\n"); ok = false; }
    }
    // 3. Doing nothing: starts with food for a while, then falls.
    {
        World w(3, true);
        int d = runDays(w, 200);
        std::printf("idle player: fell at day %d\n", d);
        if (!w.gameOver() || d < 8) { std::printf("  FAIL: idle run should fall, but not instantly\n"); ok = false; }
    }
    // 4. Disasters: economy-only vs with safety buildings.
    {
        double a = 0, b = 0;
        for (uint32_t s = 1; s <= 6; s++) {
            World w1(s, true); a += playRun(w1, 150, player);
            World w2(s, true); b += playRun(w2, 150, defender);
        }
        std::printf("with disasters (6 seeds): economy only avg %.1f days, with wells/towers/healer avg %.1f days\n", a / 6, b / 6);
        if (b < a) { std::printf("  FAIL: safety buildings should help\n"); ok = false; }
    }
    // 5. Placement rules.
    {
        World w(5, false);
        if (w.canPlace(BType::House, w.hallX, w.hallY)) { std::printf("  FAIL: placed on hall\n"); ok = false; }
        if (w.demolish(w.hallX, w.hallY)) { std::printf("  FAIL: demolished hall\n"); ok = false; }
    }
    std::printf("%s\n", ok ? "ALL OK" : "FAILED");
    return ok ? 0 : 1;
}
