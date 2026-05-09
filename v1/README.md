# vibing-with-asteroids
[May 9th 2026] I'm a reluctant vibe-coder, and much of my work is too sensitive to use AI for. But, I want to make sure I keep on top of the tech landscape as these tools evolve so I'm experimenting with vibe-coding in some of my free time. Most of this README file & the C++ code is all thanks to Claude!!

The goal This time? To play Asteroids in your CLI!!

All I prompted Claude with was "Hey Claude! Can you write another C++ program for me? I want to play Asteroids in my CLI" This took Claude significantly longer than Tetris or Snake, but it was still very fast. I think thats understandable too, considering every time I asked Calude to make a game like this, its getting more complex and/or less popular programming tasks 

## set-up
You'll need ncurses installed (`sudo apt install libncurses-dev` on Debian/Ubuntu, `brew install ncurses` on macOS — usually preinstalled).

## compile and run
```
g++ -std=c++17 -O2 asteroids.cpp -lncurses -o asteroids
./asteroids
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
> The physics uses floating-point world coordinates with momentum and friction — your ship drifts after thrusting, just like the original. Everything wraps around the screen edges (a bullet exiting the right side reappears on the left). Since terminal cells are roughly twice as tall as wide, I scale horizontal velocity by 2x so movement looks visually even rather than weirdly stretched.
>
> Asteroids come in three sizes: large @ splits into two medium O, each medium splits into two small o, and small ones disappear when shot. Points scale inversely (20/50/100) so the small fast ones are worth the risk. After clearing all asteroids, the next level spawns more of them.
>
> Lives and respawning: you start with 3 lives and get ~3 seconds of blinking invulnerability after each respawn. The ship only respawns when the center of the screen is clear of asteroids, so you don't get destroyed instantly. Hyperspace teleports you to a random spot but has a 10% chance of killing you — use sparingly.

FROM PKMH
> This is definitely the buggiest of my three experiments, but thats pretty understandable given the complexity of the game and the floating point arithmetic. Honestly, the biggest factor here is it's just hard to understand what's happening in the game because the ASCII characters -- maybe the problem are actually on me, and I shouldn't ask Claude to do this in the CLI :thinking:
>
> This game would be much more playable if it had a dedicated GUI