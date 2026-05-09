# v2
This version runs uses a simple graphics library, but has no sound. It took a bit of prompting Claude for additional help on compilation errors/warning, but they fixed it with surprising ease and speed!!

## set-up
First, do everything in `/v1` & `/v2`

Then, install SDL_mixer:
macOS: `brew install sdl2_mixer`
Ubuntu/Debian: `sudo apt install libsdl2-mixer-dev`
Windows: grab SDL2_mixer dev libs from libsdl.org

## compile and run
```
# macOS
g++ -std=c++17 -O2 asteroids_sdl.cpp $(sdl2-config --cflags --libs) -lSDL2_mixer -o asteroids_sdl
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
> Since I want sound effects without requiring you to download audio files, I'll synthesize them at runtime — generating raw PCM waveforms in code (square waves for retro shoot/explosion sounds, sine sweeps for thrust, etc). Same for the background music: a procedurally generated ambient sci-fi loop using simple synthesis. Pure C++, no external assets needed.
>
> What's new:
> Procedural sound effects — every sound is generated as raw PCM in C++ at startup, so there are no external audio files. The fire sound is a quick downward pitch sweep on a square wave for that classic arcade laser feel, explosions are filtered noise bursts (large/medium/small variants with appropriately different durations and tone), thrust is a low rumbling noise loop that plays continuously while you hold up, and hyperspace gets a descending warble. Each builds the waveform mathematically — square waves, lowpass-filtered noise, sine sweeps with vibrato.
>
> Ambient sci-fi music — a 16-second seamlessly looping track combining a three-note drone bed (A1, E2, A2) with slow volume "breathing", plus randomly scattered shimmer notes from an A minor pentatonic scale two octaves up. Each shimmer note has a bell-shaped envelope and subtle stereo panning based on its pitch for an airy, spacey feel. Soft fades at the loop boundaries prevent clicks on repeat. Toggle with M.
>
> Saucer enemies — appear periodically (first one ~25-35 seconds in, then every 15-30 seconds). Two types just like the original: the large pink saucer is slow, big, and fires randomly in any direction (worth 200 points), while the small saucer is fast, harder to hit, and aims directly at you with a small inaccuracy that decreases as your score climbs (worth 1000 points). Saucers play a continuous two-tone hum while on screen — the small saucer's hum is higher pitched and more menacing. They periodically jink vertically to dodge your shots, and they aren't immune to asteroid collisions either.
>
> Score-based difficulty — small saucers become more common as your score climbs, and their aim gets more accurate. Saucer bullets are pink to distinguish them from your yellow ones. There's also a saucer-vs-asteroid interaction: if a saucer's bullet destroys an asteroid, you don't get the points (since you didn't earn them).
The HUD now shows MUSIC and SFX status indicators in the top right so you can see what's enabled. Press M to toggle music, N to toggle sound effects.


FROM PKMH
> WOW!! Way to go Claude