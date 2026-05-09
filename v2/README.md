# v2
-- TODO

## set-up
First install SDL2:
- Ubuntu/Debian: `sudo apt install libsdl2-dev`
- macOS: `brew install sdl2`
- Windows: download SDL2 dev libs from libsdl.org

## compile and run
```
g++ -std=c++17 -O2 asteroids_sdl.cpp -lSDL2 -o asteroids_sdl
./asteroids_sdl

# macOS
g++ -std=c++17 -O2 asteroids_sdl.cpp $(sdl2-config --cflags --libs) -o asteroids_sdl
./asteroids_sdl
```

## controls
- Left / Right arrows to rotate
- Up arrow to thrust
- Space to fire
- H to hyperspace (random teleport, risky)
- P to pause
- R to restart after game over
- Q to quit

## notes
FROM CLAUDE
> Got it — the terminal version is hard to read. For an arcade-style UI you really want a graphics library. SDL2 is the classic choice for this kind of game and works on Linux, macOS, and Windows.
>
> What changed for the arcade feel:
Vector graphics — the ship is a proper triangle that rotates smoothly through any angle (not 8 fixed directions), and asteroids are jagged irregular polygons that rotate as they drift, just like the 1979 cabinet. Each asteroid has its own randomly generated outline so no two look the same.
>
> Particle effects — your engine spits out a yellow exhaust trail when you thrust, asteroids burst into debris when shot, and your ship explodes in a shower of orange sparks when you die. Particles fade out as they age.
>
> Custom vector font — I drew a 5x7 segment font from scratch (no external font files needed) so the HUD has that authentic arcade look. Score, level, and remaining ships display at the top, with little ship icons showing your lives.
>
> Polish — animated thrust flame that flickers, blinking invulnerability after respawn, a subtle starfield background, smooth 60 FPS with vsync, and proper held-key input so rotation and thrust feel continuous instead of stuttery.
The window is 1024×768. 