# A2D Village Sim

An Anno-style survival town builder in 2D pixel art for **portrait phones**. **You can't win, you can only last.**
You place buildings and roads; villagers staff them and run the production chains. The world keeps throwing
droughts, plagues, wildfires, raiders, locusts and blizzards at your town, more often the longer you survive.
There are no god powers: wells, healers and watchtowers are your only protection. Your score is the number of days survived.

<img src="docs/screenshot-phone.png" width="270" alt="Placing a well: coverage circle, covered houses light up"> <img src="docs/screenshot-chains.png" width="270" alt="Food tab and the mill selected">

## How it plays

- **Roads matter.** A building only works when a road connects it to the Town Hall. Disconnected buildings go grey with a road-sign bubble.
- **Chains:** Lumber Camp chops trees into logs, the Sawmill turns logs into planks (it keeps a few logs back for firewood and roads).
  Wheat Farm grows wheat (not in winter), the Windmill grinds flour, the Bakery bakes bread. Fishers catch fish all year.
- **People:** each villager eats one bread or fish per day. Houses (4 beds) must be on a road. Children are born when there is room and a food surplus, and start working at 6 days old.
  Workers fill jobs by priority (food first). A building stuck on a missing good shows that good in a bubble.
- **Winter** burns logs as firewood; cold houses show a firewood bubble.
- **Safety:** a Well puts out fires and stops them spreading in its radius, a Healer cures plague, a Watchtower shoots raiders.
  While placing one you see its coverage circle and the houses it would cover light up.
- Roof colours follow the drawer tabs: red-brown homes, straw food, green-brown wood, slate safety.

## Controls

Portrait-only on a 64×120 tile map; the layout adapts to any phone size (whole-number pixel scale, short side about 270 game pixels, safe-area aware).

**Touch:** the build drawer sits at the bottom. Tabs are Home, Food, Wood, Guard, Road and Del. Pick a tab, tap a building card (greyed out when you can't afford it),
drag the see-through ghost (it floats above your thumb, green where it fits and red where it doesn't), then press **Build** or **Cancel**.
With **Road** selected, drag to draw a road; the live cost shows while you drag. With **Del** selected, tap a building or road, then tap again or press Remove (half refund).
With no tool selected, one finger pans and a tap shows a building's status. Two fingers always pan and pinch-zoom.
Arrows at the screen edge point to off-screen fire (orange), plague (green) and raiders (red). The game pauses when the app goes to the background.

**Desktop (for testing):** click works like a tap, and clicking places a building directly. The ghost follows the mouse, left-drag draws roads,
right or middle drag pans, the wheel zooms, `Esc` cancels, `Space` pauses, `+`/`-` changes speed, `H` jumps to the hall and `Shift+R` starts a new world.

## Build

Needs CMake 3.20+ and a C++17 compiler. SDL3 is used from your system if found, otherwise it is downloaded and built automatically.

```sh
# macOS (optional, faster first build)
brew install sdl3

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/villagesim            # play
./build/villagesim_test       # balance/self-test (also: ctest --test-dir build)
```

`./build/villagesim --seed 10 --days 16 --demo --ui well --size 540x1170 --shot out.bmp` renders one frame at a phone resolution without opening a window. `--demo` lets a scripted player build the town first, and `--ui none|well|road|info` picks what the UI shows. `--size WxH` also sets the desktop window size, so you can preview phone layouts.

Android/iOS packaging isn't set up yet; SDL3 supports both, so that's the next build step.

## Layout

- `src/world.*`: the simulation (no SDL), covering map gen, buildings, roads, production chains, villagers, seasons and disasters.
- `src/render.*`: draws the world into a pixel buffer (8×8 px tiles, Resurrect 64 colours, day/night tint, ghost, coverage circles, need bubbles).
- `src/main.cpp`: SDL3 window, touch and mouse input, camera, top bar and build drawer.
- `src/autoplay.hpp`: a scripted player used by the self-test and `--demo` screenshots.
- `src/selftest.cpp`: balance checks. Chains produce, a peaceful town survives 60 days, an idle town falls (but not instantly), and safety buildings help against disasters.
