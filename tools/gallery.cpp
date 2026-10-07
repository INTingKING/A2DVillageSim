// Renders every building in every age side by side (art review sheet). No SDL.
// usage: villagesim_gallery out.ppm [winter]
#include "render.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace sim;

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "gallery.ppm";
    bool winter = argc > 2 && !std::strcmp(argv[2], "winter");
    const BType order[] = {BType::Hall, BType::House, BType::Lumber, BType::Sawmill, BType::Fisher, BType::Farm,
                           BType::Mill, BType::Bakery, BType::Well, BType::Healer, BType::Tower};
    const int X0 = 2, Y0 = 6, ROWW = 36, ROWH = 6;   // tiles
    std::vector<uint32_t> buf((size_t)VIEW_W * VIEW_H);
    int outW = ROWW * TILE_PX, outH = ROWH * TILE_PX * 4;
    std::vector<uint32_t> sheet((size_t)outW * outH);
    for (int a = 0; a < 4; a++) {
        World w(7, false);
        w.buildings.clear(); w.villagers.clear(); w.raiders.clear();
        for (int y = Y0 - 3; y < Y0 + ROWH; y++) for (int x = 0; x < X0 + ROWW + 2; x++) { Cell& c = w.at(x, y); c.t = Tile::Grass; c.fire = 0; c.bld = -1; }
        int x = X0 + 1;
        for (BType t : order) {
            const BInfo& in = binfo(t);
            Building b; b.type = t; b.x = x; b.y = Y0 + 3 - in.h; b.connected = true; b.staffed = 1; b.grow = 0.8f;
            int idx = (int)w.buildings.size();
            w.buildings.push_back(b);
            for (int j = 0; j < in.h; j++) for (int i = 0; i < in.w; i++) { Cell& c = w.at(x + i, b.y + j); c.t = Tile::Building; c.bld = (int16_t)idx; }
            x += in.w + 1;
        }
        for (int i = X0; i < X0 + ROWW - 1; i++) w.at(i, Y0 + 3).t = Tile::Road;
        w.store[(int)Res::Logs] = 40; w.store[(int)Res::Planks] = 30; w.store[(int)Res::Wheat] = 10;
        w.store[(int)Res::Flour] = 6; w.store[(int)Res::Bread] = 30; w.store[(int)Res::Fish] = 20;
        w.currentAge = (Age)a; w.ageCeremony = 0;
        int season = winter ? 3 : 1;
        w.ticks = TICKS_PER_DAY * DAYS_PER_SEASON * season + TICKS_PER_DAY * 4 / 10;
        Overlay ov;
        std::fill(buf.begin(), buf.end(), 0);
        drawWorld(w, buf.data(), VIEW_W, 40, ov);
        for (int y = 0; y < ROWH * TILE_PX; y++)
            for (int xx = 0; xx < outW; xx++)
                sheet[(size_t)(a * ROWH * TILE_PX + y) * outW + xx] = buf[(size_t)((Y0 - 2) * TILE_PX + y) * VIEW_W + X0 * TILE_PX + xx];
    }
    FILE* f = std::fopen(out, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", outW, outH);
    for (uint32_t p : sheet) { unsigned char c[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; std::fwrite(c, 1, 3, f); }
    std::fclose(f);
    std::printf("wrote %s (%dx%d)\n", out, outW, outH);
}
