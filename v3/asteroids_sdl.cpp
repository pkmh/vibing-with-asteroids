// Asteroids — arcade edition with sound and saucers
// Compile (Linux):   g++ -std=c++17 -O2 asteroids_sdl.cpp -lSDL2 -lSDL2_mixer -o asteroids_sdl
// Compile (macOS):   g++ -std=c++17 -O2 asteroids_sdl.cpp $(sdl2-config --cflags --libs) -lSDL2_mixer -o asteroids_sdl
// Compile (Windows): g++ -std=c++17 -O2 asteroids_sdl.cpp -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer -o asteroids_sdl.exe
//
// Requires SDL2 + SDL2_mixer:
//   Ubuntu/Debian: sudo apt install libsdl2-dev libsdl2-mixer-dev
//   macOS:         brew install sdl2 sdl2_mixer
//   Windows:       grab SDL2 + SDL2_mixer dev libs from libsdl.org
//
// All audio (sfx + music) is synthesized procedurally — no external files needed.
//
// Controls:
//   Left / Right arrows : rotate
//   Up arrow            : thrust
//   Space               : fire
//   H                   : hyperspace
//   M                   : toggle music
//   N                   : toggle sound effects
//   P                   : pause
//   R                   : restart after game over
//   Esc / Q             : quit

#include <SDL.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

constexpr int   SCREEN_W = 1024;
constexpr int   SCREEN_H = 768;
constexpr double PI = 3.14159265358979323846;

constexpr double SHIP_THRUST    = 0.15;
constexpr double SHIP_TURN_RATE = 0.10;
constexpr double SHIP_FRICTION  = 0.992;
constexpr double SHIP_MAX_SPEED = 7.0;
constexpr double BULLET_SPEED   = 9.0;
constexpr int    BULLET_LIFE    = 50;
constexpr int    FIRE_COOLDOWN  = 8;
constexpr int    RESPAWN_INVULN = 120;
constexpr int    FRAME_MS       = 16;

constexpr int    SAUCER_BULLET_LIFE  = 80;
constexpr double SAUCER_BULLET_SPEED = 5.5;

constexpr int    AUDIO_RATE = 44100;

static double frand() { return (double)std::rand() / RAND_MAX; }
static double frand(double a, double b) { return a + frand() * (b - a); }

static double wrapX(double x) {
    while (x < 0) x += SCREEN_W;
    while (x >= SCREEN_W) x -= SCREEN_W;
    return x;
}
static double wrapY(double y) {
    while (y < 0) y += SCREEN_H;
    while (y >= SCREEN_H) y -= SCREEN_H;
    return y;
}
static double dist(double x1, double y1, double x2, double y2) {
    double dx = x1 - x2, dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

// =========================================================================
//  Procedural audio synthesis
// =========================================================================
//
// SDL_mixer chunks expect 16-bit signed stereo at AUDIO_RATE. We build raw
// PCM buffers in memory, hand them to Mix_QuickLoad_RAW, and play them as
// regular Mix_Chunks. Music is just a long looping chunk on its own channel.

struct AudioBuffer {
    std::vector<Sint16> samples; // interleaved stereo
    Mix_Chunk* chunk = nullptr;

    void finalize() {
        // Mix_QuickLoad_RAW takes our buffer by pointer; we keep the vector
        // alive for the lifetime of the chunk.
        chunk = Mix_QuickLoad_RAW(
            reinterpret_cast<Uint8*>(samples.data()),
            (Uint32)(samples.size() * sizeof(Sint16))
        );
    }

    void free() {
        if (chunk) { Mix_FreeChunk(chunk); chunk = nullptr; }
        samples.clear();
    }
};

static void writeSample(AudioBuffer& buf, double l, double r) {
    auto clip = [](double v) -> Sint16 {
        if (v >  1.0) v =  1.0;
        if (v < -1.0) v = -1.0;
        return (Sint16)(v * 32000);
    };
    buf.samples.push_back(clip(l));
    buf.samples.push_back(clip(r));
}

// Fire sound: short downward pitch sweep, square wave, fast decay.
static AudioBuffer makeFireSound() {
    AudioBuffer buf;
    int dur = AUDIO_RATE / 8; // 0.125s
    double phase = 0;
    for (int i = 0; i < dur; i++) {
        double t = (double)i / dur;
        double freq = 880.0 - 600.0 * t;
        phase += 2 * PI * freq / AUDIO_RATE;
        double sq = std::sin(phase) > 0 ? 0.3 : -0.3;
        double env = std::pow(1.0 - t, 1.5);
        double s = sq * env;
        writeSample(buf, s, s);
    }
    return buf;
}

// Explosion: filtered noise with envelope. Big = lower-pitched, longer decay.
static AudioBuffer makeExplosionSound(double durationSec, double lowpass) {
    AudioBuffer buf;
    int dur = (int)(AUDIO_RATE * durationSec);
    double last = 0;
    for (int i = 0; i < dur; i++) {
        double t = (double)i / dur;
        double noise = frand(-1, 1);
        // One-pole lowpass filter for a fuller "boom" rather than pure hiss.
        last = last * (1.0 - lowpass) + noise * lowpass;
        double env = std::pow(1.0 - t, 2.0);
        double s = last * env * 0.7;
        writeSample(buf, s, s);
    }
    return buf;
}

// Thrust: low rumbling noise, lowpass-filtered.
static AudioBuffer makeThrustSound() {
    AudioBuffer buf;
    int dur = AUDIO_RATE / 6;
    double last = 0;
    for (int i = 0; i < dur; i++) {
        double noise = frand(-1, 1);
        last = last * 0.85 + noise * 0.15;
        double s = last * 0.4;
        writeSample(buf, s, s);
    }
    return buf;
}

// Hyperspace: descending warble.
static AudioBuffer makeHyperspaceSound() {
    AudioBuffer buf;
    int dur = AUDIO_RATE / 3;
    double phase = 0;
    for (int i = 0; i < dur; i++) {
        double t = (double)i / dur;
        double base = 1200.0 - 1000.0 * t;
        double mod  = std::sin(2 * PI * 18.0 * t) * 200.0;
        double freq = base + mod;
        phase += 2 * PI * freq / AUDIO_RATE;
        double s = std::sin(phase) * 0.35 * (1.0 - t);
        writeSample(buf, s, s);
    }
    return buf;
}

// Saucer engine: alternating two-tone "U-fo" hum.
static AudioBuffer makeSaucerSound(bool small) {
    AudioBuffer buf;
    int dur = AUDIO_RATE / 4;
    double phase = 0;
    double f1 = small ? 440.0 : 220.0;
    double f2 = small ? 660.0 : 330.0;
    for (int i = 0; i < dur; i++) {
        double t = (double)i / dur;
        double freq = (t < 0.5) ? f1 : f2;
        phase += 2 * PI * freq / AUDIO_RATE;
        double sq = std::sin(phase) > 0 ? 0.18 : -0.18;
        writeSample(buf, sq, sq);
    }
    return buf;
}

// Saucer firing: bright sawtooth sweep.
static AudioBuffer makeSaucerFireSound() {
    AudioBuffer buf;
    int dur = AUDIO_RATE / 10;
    double phase = 0;
    for (int i = 0; i < dur; i++) {
        double t = (double)i / dur;
        double freq = 1200.0 - 800.0 * t;
        phase += 2 * PI * freq / AUDIO_RATE;
        double saw = std::fmod(phase, 2 * PI) / PI - 1.0;
        double env = std::pow(1.0 - t, 1.3);
        double s = saw * env * 0.25;
        writeSample(buf, s, s);
    }
    return buf;
}

// Ambient sci-fi music: slow drifting drone in A minor with shimmer notes
// from a pentatonic scale. ~16 seconds, looped seamlessly.
static AudioBuffer makeAmbientMusic() {
    AudioBuffer buf;
    int dur = AUDIO_RATE * 16;

    // Drone fundamentals (A1, E2, A2) — interval stack that always reads calm.
    double drones[] = {55.0, 82.4, 110.0};
    double phases[3] = {0, 0, 0};

    // Shimmer notes — pentatonic A minor two octaves up: A4 C5 D5 E5 G5 A5
    double scale[] = {440, 523.25, 587.33, 659.25, 783.99, 880};

    struct Note { int start, length; double freq; double amp; };
    std::vector<Note> notes;
    int t = 0;
    while (t < dur) {
        Note n;
        n.start = t;
        n.length = AUDIO_RATE / 2 + std::rand() % AUDIO_RATE; // 0.5–1.5s
        n.freq = scale[std::rand() % 6];
        n.amp = frand(0.05, 0.12);
        notes.push_back(n);
        t += AUDIO_RATE / 3 + std::rand() % (AUDIO_RATE / 2);
    }

    for (int i = 0; i < dur; i++) {
        double tt = (double)i / dur;

        // Drone bed with a slow "breathing" volume modulation.
        double drone = 0;
        for (int k = 0; k < 3; k++) {
            phases[k] += 2 * PI * drones[k] / AUDIO_RATE;
            drone += std::sin(phases[k]) * 0.08;
        }
        double breath = 0.6 + 0.4 * std::sin(2 * PI * 0.05 * i / AUDIO_RATE);
        drone *= breath;

        // Shimmer notes layered on top with subtle stereo offset for width.
        double left = drone, right = drone;
        for (auto& n : notes) {
            int rel = i - n.start;
            if (rel < 0 || rel >= n.length) continue;
            double phase = 2 * PI * n.freq * rel / AUDIO_RATE;
            double nt = (double)rel / n.length;
            double env = std::sin(PI * nt); // bell-shaped swell
            double s = std::sin(phase) * n.amp * env;
            double pan = (n.freq - 440) / 440.0;
            left  += s * (1.0 - 0.3 * pan);
            right += s * (1.0 + 0.3 * pan);
        }

        // Soft fade-in/out at loop boundaries to avoid clicks on repeat.
        double loopEnv = 1.0;
        double fade = 0.05;
        if (tt < fade) loopEnv = tt / fade;
        else if (tt > 1.0 - fade) loopEnv = (1.0 - tt) / fade;
        left  *= loopEnv;
        right *= loopEnv;

        writeSample(buf, left, right);
    }
    return buf;
}

// Holds every audio buffer. Construct after Mix_OpenAudio, free before quit.
struct Audio {
    AudioBuffer fire, explLg, explMd, explSm, thrust, hyper, saucerLg, saucerSm, saucerFire, music;
    int saucerChannel = -1;
    int thrustChannel = -1;
    int musicChannel  = -1;
    bool sfxOn = true;
    bool musicOn = true;
    bool ok = false;

    void init() {
        if (Mix_OpenAudio(AUDIO_RATE, AUDIO_S16SYS, 2, 1024) < 0) {
            SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
            return;
        }
        Mix_AllocateChannels(16);

        fire       = makeFireSound();           fire.finalize();
        explLg     = makeExplosionSound(0.8, 0.05); explLg.finalize();
        explMd     = makeExplosionSound(0.5, 0.10); explMd.finalize();
        explSm     = makeExplosionSound(0.3, 0.20); explSm.finalize();
        thrust     = makeThrustSound();         thrust.finalize();
        hyper      = makeHyperspaceSound();     hyper.finalize();
        saucerLg   = makeSaucerSound(false);    saucerLg.finalize();
        saucerSm   = makeSaucerSound(true);     saucerSm.finalize();
        saucerFire = makeSaucerFireSound();     saucerFire.finalize();
        music      = makeAmbientMusic();        music.finalize();

        Mix_VolumeChunk(fire.chunk,       MIX_MAX_VOLUME / 2);
        Mix_VolumeChunk(explLg.chunk,     MIX_MAX_VOLUME);
        Mix_VolumeChunk(explMd.chunk,     MIX_MAX_VOLUME * 3 / 4);
        Mix_VolumeChunk(explSm.chunk,     MIX_MAX_VOLUME / 2);
        Mix_VolumeChunk(thrust.chunk,     MIX_MAX_VOLUME / 3);
        Mix_VolumeChunk(hyper.chunk,      MIX_MAX_VOLUME / 2);
        Mix_VolumeChunk(saucerLg.chunk,   MIX_MAX_VOLUME / 3);
        Mix_VolumeChunk(saucerSm.chunk,   MIX_MAX_VOLUME / 3);
        Mix_VolumeChunk(saucerFire.chunk, MIX_MAX_VOLUME / 2);
        Mix_VolumeChunk(music.chunk,      MIX_MAX_VOLUME * 2 / 5);

        ok = true;
    }

    void shutdown() {
        if (!ok) return;
        Mix_HaltChannel(-1);
        fire.free(); explLg.free(); explMd.free(); explSm.free();
        thrust.free(); hyper.free(); saucerLg.free(); saucerSm.free();
        saucerFire.free(); music.free();
        Mix_CloseAudio();
    }

    void play(Mix_Chunk* c) {
        if (!ok || !sfxOn || !c) return;
        Mix_PlayChannel(-1, c, 0);
    }

    void startMusic() {
        if (!ok || !musicOn || musicChannel != -1) return;
        musicChannel = Mix_PlayChannel(-1, music.chunk, -1);
    }
    void stopMusic() {
        if (musicChannel != -1) { Mix_HaltChannel(musicChannel); musicChannel = -1; }
    }
    void toggleMusic() {
        musicOn = !musicOn;
        if (musicOn) startMusic(); else stopMusic();
    }

    void startThrust() {
        if (!ok || !sfxOn || thrustChannel != -1) return;
        thrustChannel = Mix_PlayChannel(-1, thrust.chunk, -1);
    }
    void stopThrust() {
        if (thrustChannel != -1) { Mix_HaltChannel(thrustChannel); thrustChannel = -1; }
    }

    void startSaucer(bool small) {
        if (!ok || !sfxOn || saucerChannel != -1) return;
        saucerChannel = Mix_PlayChannel(-1, small ? saucerSm.chunk : saucerLg.chunk, -1);
    }
    void stopSaucer() {
        if (saucerChannel != -1) { Mix_HaltChannel(saucerChannel); saucerChannel = -1; }
    }
};

// =========================================================================
//  Vector font (5x7 hand-drawn segments)
// =========================================================================
struct Glyph { std::vector<int> seg; };
static Glyph G(std::vector<int> s) { return {s}; }

static const Glyph& glyph(char c) {
    static Glyph table[128];
    static bool init = false;
    if (!init) {
        init = true;
        table['0'] = G({0,0, 4,0, 4,0, 4,6, 4,6, 0,6, 0,6, 0,0, 0,6, 4,0});
        table['1'] = G({2,0, 2,6, 0,6, 4,6, 1,1, 2,0});
        table['2'] = G({0,0, 4,0, 4,0, 4,3, 4,3, 0,3, 0,3, 0,6, 0,6, 4,6});
        table['3'] = G({0,0, 4,0, 4,0, 4,6, 4,6, 0,6, 0,3, 4,3});
        table['4'] = G({0,0, 0,3, 0,3, 4,3, 4,0, 4,6});
        table['5'] = G({4,0, 0,0, 0,0, 0,3, 0,3, 4,3, 4,3, 4,6, 4,6, 0,6});
        table['6'] = G({4,0, 0,0, 0,0, 0,6, 0,6, 4,6, 4,6, 4,3, 4,3, 0,3});
        table['7'] = G({0,0, 4,0, 4,0, 4,6});
        table['8'] = G({0,0, 4,0, 4,0, 4,6, 4,6, 0,6, 0,6, 0,0, 0,3, 4,3});
        table['9'] = G({4,6, 4,0, 4,0, 0,0, 0,0, 0,3, 0,3, 4,3});
        table['A'] = G({0,6, 0,0, 0,0, 4,0, 4,0, 4,6, 0,3, 4,3});
        table['B'] = G({0,0, 0,6, 0,0, 4,0, 4,0, 4,3, 4,3, 0,3, 0,3, 4,3, 4,3, 4,6, 4,6, 0,6});
        table['C'] = G({4,0, 0,0, 0,0, 0,6, 0,6, 4,6});
        table['D'] = G({0,0, 0,6, 0,0, 3,0, 3,0, 4,1, 4,1, 4,5, 4,5, 3,6, 3,6, 0,6});
        table['E'] = G({4,0, 0,0, 0,0, 0,6, 0,6, 4,6, 0,3, 3,3});
        table['F'] = G({4,0, 0,0, 0,0, 0,6, 0,3, 3,3});
        table['G'] = G({4,0, 0,0, 0,0, 0,6, 0,6, 4,6, 4,6, 4,3, 4,3, 2,3});
        table['H'] = G({0,0, 0,6, 4,0, 4,6, 0,3, 4,3});
        table['I'] = G({0,0, 4,0, 2,0, 2,6, 0,6, 4,6});
        table['J'] = G({4,0, 4,6, 4,6, 0,6, 0,6, 0,4});
        table['K'] = G({0,0, 0,6, 0,3, 4,0, 0,3, 4,6});
        table['L'] = G({0,0, 0,6, 0,6, 4,6});
        table['M'] = G({0,6, 0,0, 0,0, 2,2, 2,2, 4,0, 4,0, 4,6});
        table['N'] = G({0,6, 0,0, 0,0, 4,6, 4,6, 4,0});
        table['O'] = G({0,0, 4,0, 4,0, 4,6, 4,6, 0,6, 0,6, 0,0});
        table['P'] = G({0,6, 0,0, 0,0, 4,0, 4,0, 4,3, 4,3, 0,3});
        table['Q'] = G({0,0, 4,0, 4,0, 4,6, 4,6, 0,6, 0,6, 0,0, 2,4, 4,6});
        table['R'] = G({0,6, 0,0, 0,0, 4,0, 4,0, 4,3, 4,3, 0,3, 0,3, 4,6});
        table['S'] = G({4,0, 0,0, 0,0, 0,3, 0,3, 4,3, 4,3, 4,6, 4,6, 0,6});
        table['T'] = G({0,0, 4,0, 2,0, 2,6});
        table['U'] = G({0,0, 0,6, 0,6, 4,6, 4,6, 4,0});
        table['V'] = G({0,0, 2,6, 2,6, 4,0});
        table['W'] = G({0,0, 0,6, 0,6, 2,4, 2,4, 4,6, 4,6, 4,0});
        table['X'] = G({0,0, 4,6, 4,0, 0,6});
        table['Y'] = G({0,0, 2,3, 4,0, 2,3, 2,3, 2,6});
        table['Z'] = G({0,0, 4,0, 4,0, 0,6, 0,6, 4,6});
        table[' '] = G({});
        table[':'] = G({2,1, 2,2, 2,4, 2,5});
        table['.'] = G({2,5, 2,6});
        table['-'] = G({1,3, 3,3});
        table['/'] = G({4,0, 0,6});
        table['!'] = G({2,0, 2,4, 2,5, 2,6});
        for (char c = 'a'; c <= 'z'; c++) table[(int)c] = table[c - 'a' + 'A'];
    }
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) return table[' '];
    return table[uc];
}

static void drawText(SDL_Renderer* r, const std::string& s, int x, int y, int scale,
                     Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    int cx = x;
    for (char c : s) {
        const Glyph& g = glyph(c);
        for (size_t i = 0; i + 3 < g.seg.size(); i += 4) {
            SDL_RenderDrawLine(r,
                cx + g.seg[i] * scale, y + g.seg[i+1] * scale,
                cx + g.seg[i+2] * scale, y + g.seg[i+3] * scale);
        }
        cx += 6 * scale;
    }
}

// =========================================================================
//  Game objects
// =========================================================================
struct Ship {
    double x, y, vx = 0, vy = 0;
    double angle = -PI / 2;
    bool alive = true;
    int invuln = RESPAWN_INVULN;
    int cooldown = 0;
    bool thrusting = false;
};

struct Bullet { double x, y, vx, vy; int life; bool fromSaucer = false; };

struct Asteroid {
    double x, y, vx, vy;
    double rot, vrot;
    int size;
    std::vector<double> shape;
};

struct Saucer {
    double x, y, vx, vy;
    bool small;        // true = small accurate saucer, false = big random saucer
    int fireTimer;
    int directionTimer;
    bool alive = false;
};

struct Particle {
    double x, y, vx, vy;
    int life, maxLife;
    Uint8 r, g, b;
};

// =========================================================================
//  Game
// =========================================================================
struct Game {
    Audio* audio = nullptr;
    Ship ship;
    std::vector<Bullet> bullets;
    std::vector<Asteroid> asteroids;
    std::vector<Particle> particles;
    Saucer saucer;
    int saucerSpawnTimer = 0;
    int score = 0;
    int lives = 3;
    int level = 1;
    bool over = false;
    bool paused = false;

    Game() {
        std::srand((unsigned)std::time(nullptr));
        reset();
    }

    void reset() {
        score = 0; lives = 3; level = 1;
        over = false; paused = false;
        bullets.clear();
        particles.clear();
        saucer.alive = false;
        if (audio) audio->stopSaucer();
        saucerSpawnTimer = 1500 + std::rand() % 600; // ~25–35s before first saucer
        spawnShip();
        startLevel();
    }

    void spawnShip() {
        ship.x = SCREEN_W / 2.0;
        ship.y = SCREEN_H / 2.0;
        ship.vx = ship.vy = 0;
        ship.angle = -PI / 2;
        ship.alive = true;
        ship.invuln = RESPAWN_INVULN;
        ship.cooldown = 0;
        ship.thrusting = false;
    }

    Asteroid makeAsteroid(double x, double y, int size) {
        Asteroid a;
        a.x = x; a.y = y;
        double ang = frand(0, 2 * PI);
        double speed = frand(0.5, 1.5) + (3 - size) * 0.6;
        a.vx = std::cos(ang) * speed;
        a.vy = std::sin(ang) * speed;
        a.rot = 0;
        a.vrot = frand(-0.04, 0.04);
        a.size = size;
        int verts = 12;
        double baseR = (size == 3 ? 50 : size == 2 ? 28 : 15);
        for (int i = 0; i < verts; i++) {
            a.shape.push_back(baseR * frand(0.75, 1.15));
        }
        return a;
    }

    void startLevel() {
        asteroids.clear();
        int count = 3 + level;
        for (int i = 0; i < count; i++) {
            double x, y;
            do {
                x = frand(0, SCREEN_W);
                y = frand(0, SCREEN_H);
            } while (dist(x, y, ship.x, ship.y) < 150);
            asteroids.push_back(makeAsteroid(x, y, 3));
        }
    }

    double asteroidRadius(const Asteroid& a) const {
        return a.size == 3 ? 50 : a.size == 2 ? 28 : 15;
    }
    int asteroidPoints(int size) const {
        return size == 3 ? 20 : size == 2 ? 50 : 100;
    }

    void rotate(double d) { ship.angle += d; }

    void thrust() {
        ship.vx += std::cos(ship.angle) * SHIP_THRUST;
        ship.vy += std::sin(ship.angle) * SHIP_THRUST;
        double sp = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy);
        if (sp > SHIP_MAX_SPEED) {
            ship.vx *= SHIP_MAX_SPEED / sp;
            ship.vy *= SHIP_MAX_SPEED / sp;
        }
        ship.thrusting = true;

        for (int i = 0; i < 2; i++) {
            Particle p;
            double back = ship.angle + PI;
            double spread = frand(-0.3, 0.3);
            p.x = ship.x + std::cos(back) * 12;
            p.y = ship.y + std::sin(back) * 12;
            double ps = frand(1.5, 3.0);
            p.vx = std::cos(back + spread) * ps + ship.vx * 0.3;
            p.vy = std::sin(back + spread) * ps + ship.vy * 0.3;
            p.life = p.maxLife = 20;
            p.r = 255; p.g = (Uint8)frand(150, 220); p.b = 60;
            particles.push_back(p);
        }
    }

    void fire() {
        if (!ship.alive || ship.cooldown > 0) return;
        Bullet b;
        b.x = ship.x + std::cos(ship.angle) * 14;
        b.y = ship.y + std::sin(ship.angle) * 14;
        b.vx = std::cos(ship.angle) * BULLET_SPEED + ship.vx * 0.3;
        b.vy = std::sin(ship.angle) * BULLET_SPEED + ship.vy * 0.3;
        b.life = BULLET_LIFE;
        b.fromSaucer = false;
        bullets.push_back(b);
        ship.cooldown = FIRE_COOLDOWN;
        if (audio) audio->play(audio->fire.chunk);
    }

    void hyperspace() {
        if (!ship.alive) return;
        if (audio) audio->play(audio->hyper.chunk);
        ship.x = frand(0, SCREEN_W);
        ship.y = frand(0, SCREEN_H);
        ship.vx = ship.vy = 0;
        if (frand() < 0.10) killShip(true);
    }

    void spawnExplosion(double x, double y, int count, Uint8 r, Uint8 g, Uint8 b) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.x = x; p.y = y;
            double ang = frand(0, 2 * PI);
            double sp = frand(1.0, 4.5);
            p.vx = std::cos(ang) * sp;
            p.vy = std::sin(ang) * sp;
            p.life = p.maxLife = (int)frand(25, 55);
            p.r = r; p.g = g; p.b = b;
            particles.push_back(p);
        }
    }

    void killShip(bool fromHyperspace = false) {
        if (!ship.alive || (ship.invuln > 0 && !fromHyperspace)) return;
        spawnExplosion(ship.x, ship.y, 30, 255, 200, 100);
        if (audio) audio->play(audio->explLg.chunk);
        ship.alive = false;
        lives--;
        if (lives <= 0) over = true;
    }

    void splitAsteroid(size_t i) {
        Asteroid a = asteroids[i];
        score += asteroidPoints(a.size);
        spawnExplosion(a.x, a.y, 15, 200, 200, 220);
        if (audio) {
            if (a.size == 3) audio->play(audio->explLg.chunk);
            else if (a.size == 2) audio->play(audio->explMd.chunk);
            else audio->play(audio->explSm.chunk);
        }
        asteroids.erase(asteroids.begin() + i);
        if (a.size > 1) {
            for (int k = 0; k < 2; k++) {
                asteroids.push_back(makeAsteroid(a.x, a.y, a.size - 1));
            }
        }
    }

    // ---------- Saucers ----------
    void spawnSaucer() {
        // Small saucer becomes more likely as score climbs.
        bool small = frand() < std::min(0.7, 0.2 + score / 5000.0);
        saucer.small = small;
        saucer.alive = true;
        bool fromLeft = (std::rand() % 2) == 0;
        saucer.x = fromLeft ? -20 : SCREEN_W + 20;
        saucer.y = frand(50, SCREEN_H - 50);
        double speed = small ? 3.2 : 2.2;
        saucer.vx = (fromLeft ? 1 : -1) * speed;
        saucer.vy = 0;
        saucer.fireTimer = 60 + std::rand() % 60;
        saucer.directionTimer = 90 + std::rand() % 60;
        if (audio) audio->startSaucer(small);
    }

    void killSaucer(bool byPlayer) {
        if (!saucer.alive) return;
        spawnExplosion(saucer.x, saucer.y, 20, 255, 150, 200);
        if (audio) {
            audio->play(audio->explMd.chunk);
            audio->stopSaucer();
        }
        if (byPlayer) score += saucer.small ? 1000 : 200;
        saucer.alive = false;
        saucerSpawnTimer = 900 + std::rand() % 900; // 15–30s until next saucer
    }

    void saucerFire() {
        Bullet b;
        b.x = saucer.x;
        b.y = saucer.y;
        double ang;
        if (saucer.small) {
            // Aim at the player with shrinking inaccuracy as score climbs.
            double inacc = std::max(0.05, 0.5 - score / 20000.0);
            ang = std::atan2(ship.y - saucer.y, ship.x - saucer.x) + frand(-inacc, inacc);
        } else {
            ang = frand(0, 2 * PI);
        }
        b.vx = std::cos(ang) * SAUCER_BULLET_SPEED;
        b.vy = std::sin(ang) * SAUCER_BULLET_SPEED;
        b.life = SAUCER_BULLET_LIFE;
        b.fromSaucer = true;
        bullets.push_back(b);
        if (audio) audio->play(audio->saucerFire.chunk);
    }

    void updateSaucer() {
        if (!saucer.alive) {
            saucerSpawnTimer--;
            if (saucerSpawnTimer <= 0 && asteroids.size() > 1) spawnSaucer();
            return;
        }

        saucer.x += saucer.vx;
        saucer.y += saucer.vy;

        // Saucers don't wrap left/right — they leave the screen and despawn.
        if (saucer.x < -30 || saucer.x > SCREEN_W + 30) {
            saucer.alive = false;
            if (audio) audio->stopSaucer();
            saucerSpawnTimer = 900 + std::rand() % 900;
            return;
        }
        // Vertical wrap is fine.
        saucer.y = wrapY(saucer.y);

        // Periodic vertical jinks make it harder to hit.
        if (--saucer.directionTimer <= 0) {
            saucer.vy = frand(-1.5, 1.5);
            saucer.directionTimer = 90 + std::rand() % 90;
        }

        if (--saucer.fireTimer <= 0) {
            saucerFire();
            saucer.fireTimer = saucer.small ? (45 + std::rand() % 30)
                                            : (75 + std::rand() % 60);
        }
    }

    void update() {
        if (over || paused) return;

        ship.thrusting = false;

        if (ship.alive) {
            ship.vx *= SHIP_FRICTION;
            ship.vy *= SHIP_FRICTION;
            ship.x = wrapX(ship.x + ship.vx);
            ship.y = wrapY(ship.y + ship.vy);
            if (ship.cooldown > 0) ship.cooldown--;
            if (ship.invuln > 0) ship.invuln--;
        } else {
            bool safe = true;
            double cx = SCREEN_W / 2.0, cy = SCREEN_H / 2.0;
            for (auto& a : asteroids) {
                if (dist(cx, cy, a.x, a.y) < 120) { safe = false; break; }
            }
            if (safe && lives > 0) spawnShip();
        }

        for (auto it = bullets.begin(); it != bullets.end(); ) {
            it->x = wrapX(it->x + it->vx);
            it->y = wrapY(it->y + it->vy);
            it->life--;
            if (it->life <= 0) it = bullets.erase(it);
            else ++it;
        }

        for (auto& a : asteroids) {
            a.x = wrapX(a.x + a.vx);
            a.y = wrapY(a.y + a.vy);
            a.rot += a.vrot;
        }

        for (auto it = particles.begin(); it != particles.end(); ) {
            it->x += it->vx;
            it->y += it->vy;
            it->vx *= 0.97;
            it->vy *= 0.97;
            it->life--;
            if (it->life <= 0) it = particles.erase(it);
            else ++it;
        }

        updateSaucer();

        // Bullet vs asteroid (any bullet, including the saucer's, can break asteroids)
        for (size_t i = 0; i < bullets.size(); ) {
            bool hit = false;
            for (size_t j = 0; j < asteroids.size(); j++) {
                if (dist(bullets[i].x, bullets[i].y, asteroids[j].x, asteroids[j].y)
                        < asteroidRadius(asteroids[j])) {
                    bool playerKill = !bullets[i].fromSaucer;
                    Asteroid a = asteroids[j];
                    splitAsteroid(j);
                    // Saucer-killed asteroids shouldn't grant points.
                    if (!playerKill) score -= asteroidPoints(a.size);
                    hit = true;
                    break;
                }
            }
            if (hit) { bullets.erase(bullets.begin() + i); continue; }

            // Player bullet can hit saucer.
            if (saucer.alive && !bullets[i].fromSaucer) {
                double sr = saucer.small ? 12 : 20;
                if (dist(bullets[i].x, bullets[i].y, saucer.x, saucer.y) < sr) {
                    killSaucer(true);
                    bullets.erase(bullets.begin() + i);
                    continue;
                }
            }
            i++;
        }

        // Saucer bullets vs ship
        if (ship.alive && ship.invuln == 0) {
            for (size_t i = 0; i < bullets.size(); ) {
                if (bullets[i].fromSaucer && dist(bullets[i].x, bullets[i].y, ship.x, ship.y) < 8) {
                    killShip();
                    bullets.erase(bullets.begin() + i);
                    break;
                }
                i++;
            }
        }
        // Ship vs asteroid
        if (ship.alive && ship.invuln == 0) {
            for (auto& a : asteroids) {
                if (dist(ship.x, ship.y, a.x, a.y) < asteroidRadius(a) + 8) {
                    killShip();
                    break;
                }
            }
        }
        // Ship vs saucer
        if (ship.alive && ship.invuln == 0 && saucer.alive) {
            double sr = saucer.small ? 12 : 20;
            if (dist(ship.x, ship.y, saucer.x, saucer.y) < sr + 8) {
                killShip();
                killSaucer(false);
            }
        }

        // Asteroid vs saucer — saucers aren't immune to rocks.
        if (saucer.alive) {
            double sr = saucer.small ? 12 : 20;
            for (auto& a : asteroids) {
                if (dist(saucer.x, saucer.y, a.x, a.y) < asteroidRadius(a) + sr) {
                    killSaucer(false);
                    break;
                }
            }
        }

        if (asteroids.empty()) {
            level++;
            startLevel();
        }
    }
};

// =========================================================================
//  Rendering
// =========================================================================
static void drawShipShape(SDL_Renderer* r, double x, double y, double angle,
                          Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    double nx = std::cos(angle), ny = std::sin(angle);
    double px = -ny, py = nx;
    double nose_x = x + nx * 14, nose_y = y + ny * 14;
    double bl_x = x - nx * 8 + px * 8, bl_y = y - ny * 8 + py * 8;
    double br_x = x - nx * 8 - px * 8, br_y = y - ny * 8 - py * 8;
    double bk_x = x - nx * 4,        bk_y = y - ny * 4;
    SDL_RenderDrawLine(r, (int)nose_x, (int)nose_y, (int)bl_x, (int)bl_y);
    SDL_RenderDrawLine(r, (int)nose_x, (int)nose_y, (int)br_x, (int)br_y);
    SDL_RenderDrawLine(r, (int)bl_x, (int)bl_y, (int)bk_x, (int)bk_y);
    SDL_RenderDrawLine(r, (int)br_x, (int)br_y, (int)bk_x, (int)bk_y);
}

static void drawShipAt(SDL_Renderer* r, const Ship& s, double x, double y) {
    drawShipShape(r, x, y, s.angle, 220, 240, 255);
    if (s.thrusting) {
        SDL_SetRenderDrawColor(r, 255, 160, 40, 255);
        double back = s.angle + PI;
        double bx = x + std::cos(back) * 4;
        double by = y + std::sin(back) * 4;
        double tipx = bx + std::cos(back) * frand(8, 14);
        double tipy = by + std::sin(back) * frand(8, 14);
        double px = -std::sin(s.angle), py = std::cos(s.angle);
        SDL_RenderDrawLine(r, (int)(bx + px * 4), (int)(by + py * 4), (int)tipx, (int)tipy);
        SDL_RenderDrawLine(r, (int)(bx - px * 4), (int)(by - py * 4), (int)tipx, (int)tipy);
    }
}

static void drawAsteroid(SDL_Renderer* r, const Asteroid& a) {
    SDL_SetRenderDrawColor(r, 180, 180, 200, 255);
    int n = (int)a.shape.size();
    for (int i = 0; i < n; i++) {
        double a1 = a.rot + (2 * PI * i) / n;
        double a2 = a.rot + (2 * PI * (i + 1)) / n;
        double x1 = a.x + std::cos(a1) * a.shape[i];
        double y1 = a.y + std::sin(a1) * a.shape[i];
        double x2 = a.x + std::cos(a2) * a.shape[(i + 1) % n];
        double y2 = a.y + std::sin(a2) * a.shape[(i + 1) % n];
        SDL_RenderDrawLine(r, (int)x1, (int)y1, (int)x2, (int)y2);
    }
}

// Classic flying-saucer silhouette: top dome + middle disc.
static void drawSaucer(SDL_Renderer* r, const Saucer& s) {
    SDL_SetRenderDrawColor(r, 255, 120, 220, 255);
    double scale = s.small ? 0.55 : 1.0;
    double x = s.x, y = s.y;
    auto L = [&](double ax, double ay, double bx, double by) {
        SDL_RenderDrawLine(r,
            (int)(x + ax * scale), (int)(y + ay * scale),
            (int)(x + bx * scale), (int)(y + by * scale));
    };
    // Middle disc — long horizontal hexagon
    L(-20, 0,  -10, -5);
    L(-10, -5,  10, -5);
    L( 10, -5,  20,  0);
    L( 20,  0,  10,  5);
    L( 10,  5, -10,  5);
    L(-10,  5, -20,  0);
    // Top dome
    L(-10, -5,  -5, -10);
    L( -5,-10,   5,-10);
    L(  5,-10,  10, -5);
    // Detail line through the body
    L(-10,  0,  10,  0);
}

static void render(SDL_Renderer* r, const Game& g) {
    SDL_SetRenderDrawColor(r, 5, 5, 15, 255);
    SDL_RenderClear(r);

    SDL_SetRenderDrawColor(r, 80, 80, 100, 255);
    unsigned seed = 12345;
    for (int i = 0; i < 80; i++) {
        seed = seed * 1103515245 + 12345;
        int sx = (seed >> 8) % SCREEN_W;
        seed = seed * 1103515245 + 12345;
        int sy = (seed >> 8) % SCREEN_H;
        SDL_RenderDrawPoint(r, sx, sy);
    }

    for (auto& p : g.particles) {
        Uint8 alpha = (Uint8)(255.0 * p.life / p.maxLife);
        SDL_SetRenderDrawColor(r, p.r, p.g, p.b, alpha);
        SDL_Rect rect{(int)p.x, (int)p.y, 2, 2};
        SDL_RenderFillRect(r, &rect);
    }

    for (auto& a : g.asteroids) drawAsteroid(r, a);

    if (g.saucer.alive) drawSaucer(r, g.saucer);

    for (auto& b : g.bullets) {
        if (b.fromSaucer) SDL_SetRenderDrawColor(r, 255, 100, 200, 255);
        else              SDL_SetRenderDrawColor(r, 255, 255, 120, 255);
        SDL_Rect rect{(int)b.x - 1, (int)b.y - 1, 3, 3};
        SDL_RenderFillRect(r, &rect);
    }

    if (g.ship.alive) {
        bool show = g.ship.invuln == 0 || (g.ship.invuln / 6) % 2 == 0;
        if (show) drawShipAt(r, g.ship, g.ship.x, g.ship.y);
    }

    drawText(r, "SCORE  " + std::to_string(g.score), 20, 20, 3, 0, 255, 200);
    drawText(r, "LEVEL  " + std::to_string(g.level), SCREEN_W - 240, 20, 3, 0, 255, 200);

    drawText(r, "SHIPS", 20, 60, 2, 200, 200, 220);
    for (int i = 0; i < g.lives; i++) {
        drawShipShape(r, 130 + i * 28, 70, -PI / 2, 200, 200, 220);
    }

    drawText(r, std::string("MUSIC ") + (g.audio && g.audio->musicOn ? "ON " : "OFF"),
             SCREEN_W - 240, 60, 2, 150, 150, 170);
    drawText(r, std::string("SFX   ") + (g.audio && g.audio->sfxOn ? "ON " : "OFF"),
             SCREEN_W - 240, 80, 2, 150, 150, 170);

    if (g.paused) {
        drawText(r, "PAUSED", SCREEN_W / 2 - 90, SCREEN_H / 2 - 20, 5, 255, 255, 100);
    }
    if (g.over) {
        drawText(r, "GAME OVER", SCREEN_W / 2 - 135, SCREEN_H / 2 - 40, 5, 255, 80, 80);
        drawText(r, "PRESS R TO RESTART", SCREEN_W / 2 - 162, SCREEN_H / 2 + 20, 3, 200, 200, 200);
    }

    drawText(r, "ARROWS ROTATE/THRUST  SPACE FIRE  H HYPERSPACE  M MUSIC  N SFX  P PAUSE  Q QUIT",
             20, SCREEN_H - 30, 2, 120, 140, 160);

    SDL_RenderPresent(r);
}

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("Asteroids",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!win) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Audio audio;
    audio.init();

    Game g;
    g.audio = &audio;
    audio.startMusic();

    bool running = true;
    Uint32 lastTick = SDL_GetTicks();
    bool prevThrusting = false;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) running = false;
                else if (k == SDLK_p) { if (!g.over) g.paused = !g.paused; }
                else if (k == SDLK_r) { if (g.over) g.reset(); }
                else if (k == SDLK_m) { audio.toggleMusic(); }
                else if (k == SDLK_n) {
                    audio.sfxOn = !audio.sfxOn;
                    if (!audio.sfxOn) { audio.stopThrust(); audio.stopSaucer(); }
                    else if (g.saucer.alive) audio.startSaucer(g.saucer.small);
                }
                else if (!g.paused && !g.over && g.ship.alive) {
                    if (k == SDLK_SPACE) g.fire();
                    else if (k == SDLK_h) g.hyperspace();
                }
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(NULL);
        if (g.ship.alive && !g.paused && !g.over) {
            if (keys[SDL_SCANCODE_LEFT])  g.rotate(-SHIP_TURN_RATE);
            if (keys[SDL_SCANCODE_RIGHT]) g.rotate( SHIP_TURN_RATE);
            if (keys[SDL_SCANCODE_UP])    g.thrust();
        }

        Uint32 now = SDL_GetTicks();
        if (now - lastTick >= FRAME_MS) {
            g.update();
            lastTick = now;

            // Start/stop the looping thrust sound based on this frame's input.
            if (g.ship.thrusting && !prevThrusting) audio.startThrust();
            else if (!g.ship.thrusting && prevThrusting) audio.stopThrust();
            prevThrusting = g.ship.thrusting;

            if (!g.saucer.alive && audio.saucerChannel != -1) audio.stopSaucer();
        }

        render(ren, g);
    }

    audio.shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
