// Asteroids — arcade edition
// Compile (Linux):   g++ -std=c++17 -O2 asteroids_sdl.cpp -lSDL2 -o asteroids_sdl
// Compile (macOS):   g++ -std=c++17 -O2 asteroids_sdl.cpp $(sdl2-config --cflags --libs) -o asteroids_sdl
// Compile (Windows): g++ -std=c++17 -O2 asteroids_sdl.cpp -lmingw32 -lSDL2main -lSDL2 -o asteroids_sdl.exe
//
// Requires SDL2:
//   Ubuntu/Debian: sudo apt install libsdl2-dev
//   macOS:         brew install sdl2
//   Windows:       grab SDL2 dev libs from libsdl.org
//
// Controls:
//   Left / Right arrows : rotate
//   Up arrow            : thrust
//   Space               : fire
//   H                   : hyperspace
//   P                   : pause
//   R                   : restart after game over
//   Esc / Q             : quit

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

constexpr int   SCREEN_W = 1024;
constexpr int   SCREEN_H = 768;
constexpr double PI = 3.14159265358979323846;

constexpr double SHIP_THRUST    = 0.15;
constexpr double SHIP_TURN_RATE = 0.10;     // radians per frame
constexpr double SHIP_FRICTION  = 0.992;
constexpr double SHIP_MAX_SPEED = 7.0;
constexpr double BULLET_SPEED   = 9.0;
constexpr int    BULLET_LIFE    = 50;       // frames
constexpr int    FIRE_COOLDOWN  = 8;
constexpr int    RESPAWN_INVULN = 120;
constexpr int    FRAME_MS       = 16;       // ~60 FPS

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

// ---------- Vector font (5x7 hand-drawn segments) ----------
// Each char is a list of line segments in a 5x7 grid (x: 0-4, y: 0-6).
// Pairs of points: {x0, y0, x1, y1, ...}. Empty = blank.
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
        // lowercase falls back to uppercase
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

// ---------- Game objects ----------
struct Ship {
    double x, y;
    double vx = 0, vy = 0;
    double angle = -PI / 2;
    bool alive = true;
    int invuln = RESPAWN_INVULN;
    int cooldown = 0;
    bool thrusting = false;
};

struct Bullet { double x, y, vx, vy; int life; };

struct Asteroid {
    double x, y, vx, vy;
    double rot, vrot;
    int size;            // 3 large, 2 medium, 1 small
    std::vector<double> shape; // radii at evenly-spaced angles for jagged outline
};

struct Particle {
    double x, y, vx, vy;
    int life, maxLife;
    Uint8 r, g, b;
};

// ---------- Game ----------
struct Game {
    Ship ship;
    std::vector<Bullet> bullets;
    std::vector<Asteroid> asteroids;
    std::vector<Particle> particles;
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
        // Smaller fragments fly faster; size 3 is sluggish.
        double speed = frand(0.5, 1.5) + (3 - size) * 0.6;
        a.vx = std::cos(ang) * speed;
        a.vy = std::sin(ang) * speed;
        a.rot = 0;
        a.vrot = frand(-0.04, 0.04);
        a.size = size;

        // Jagged outline: 12 vertices with random radius variation around a base.
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

        // Exhaust particles trailing behind the ship.
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
        bullets.push_back(b);
        ship.cooldown = FIRE_COOLDOWN;
    }

    void hyperspace() {
        if (!ship.alive) return;
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
        ship.alive = false;
        lives--;
        if (lives <= 0) over = true;
    }

    void splitAsteroid(size_t i) {
        Asteroid a = asteroids[i];
        score += asteroidPoints(a.size);
        spawnExplosion(a.x, a.y, 15, 200, 200, 220);
        asteroids.erase(asteroids.begin() + i);
        if (a.size > 1) {
            for (int k = 0; k < 2; k++) {
                asteroids.push_back(makeAsteroid(a.x, a.y, a.size - 1));
            }
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
            // Wait for a clear center before respawning.
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

        // Bullet vs asteroid
        for (size_t i = 0; i < bullets.size(); ) {
            bool hit = false;
            for (size_t j = 0; j < asteroids.size(); j++) {
                if (dist(bullets[i].x, bullets[i].y, asteroids[j].x, asteroids[j].y)
                        < asteroidRadius(asteroids[j])) {
                    splitAsteroid(j);
                    hit = true;
                    break;
                }
            }
            if (hit) bullets.erase(bullets.begin() + i);
            else i++;
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

        if (asteroids.empty()) {
            level++;
            startLevel();
        }
    }
};

// ---------- Rendering ----------
static void drawShipShape(SDL_Renderer* r, double x, double y, double angle,
                          Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    // Triangle: nose, back-left, back-right, plus a notch at the back.
    double nx = std::cos(angle);
    double ny = std::sin(angle);
    // perpendicular
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
        // Flame flickers behind the ship.
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

static void render(SDL_Renderer* r, const Game& g) {
    // Background
    SDL_SetRenderDrawColor(r, 5, 5, 15, 255);
    SDL_RenderClear(r);

    // Faint starfield (deterministic — re-seeded each frame from a fixed value).
    SDL_SetRenderDrawColor(r, 80, 80, 100, 255);
    unsigned seed = 12345;
    for (int i = 0; i < 80; i++) {
        seed = seed * 1103515245 + 12345;
        int sx = (seed >> 8) % SCREEN_W;
        seed = seed * 1103515245 + 12345;
        int sy = (seed >> 8) % SCREEN_H;
        SDL_RenderDrawPoint(r, sx, sy);
    }

    // Particles
    for (auto& p : g.particles) {
        Uint8 alpha = (Uint8)(255.0 * p.life / p.maxLife);
        SDL_SetRenderDrawColor(r, p.r, p.g, p.b, alpha);
        // A 2x2 cluster reads better than a single pixel.
        SDL_Rect rect{(int)p.x, (int)p.y, 2, 2};
        SDL_RenderFillRect(r, &rect);
    }

    // Asteroids
    for (auto& a : g.asteroids) drawAsteroid(r, a);

    // Bullets
    SDL_SetRenderDrawColor(r, 255, 255, 120, 255);
    for (auto& b : g.bullets) {
        SDL_Rect rect{(int)b.x - 1, (int)b.y - 1, 3, 3};
        SDL_RenderFillRect(r, &rect);
    }

    // Ship (blink during invulnerability)
    if (g.ship.alive) {
        bool show = g.ship.invuln == 0 || (g.ship.invuln / 6) % 2 == 0;
        if (show) drawShipAt(r, g.ship, g.ship.x, g.ship.y);
    }

    // HUD
    drawText(r, "SCORE  " + std::to_string(g.score), 20, 20, 3, 0, 255, 200);
    drawText(r, "LEVEL  " + std::to_string(g.level), SCREEN_W - 240, 20, 3, 0, 255, 200);

    // Lives shown as little ship icons in the top-left under the score.
    drawText(r, "SHIPS", 20, 60, 2, 200, 200, 220);
    for (int i = 0; i < g.lives; i++) {
        drawShipShape(r, 130 + i * 28, 70, -PI / 2, 200, 200, 220);
    }

    if (g.paused) {
        drawText(r, "PAUSED", SCREEN_W / 2 - 90, SCREEN_H / 2 - 20, 5, 255, 255, 100);
    }
    if (g.over) {
        drawText(r, "GAME OVER", SCREEN_W / 2 - 135, SCREEN_H / 2 - 40, 5, 255, 80, 80);
        drawText(r, "PRESS R TO RESTART", SCREEN_W / 2 - 162, SCREEN_H / 2 + 20, 3, 200, 200, 200);
    }

    // Footer with controls
    drawText(r, "ARROWS ROTATE/THRUST   SPACE FIRE   H HYPERSPACE   P PAUSE   Q QUIT",
             20, SCREEN_H - 30, 2, 120, 140, 160);

    SDL_RenderPresent(r);
}

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
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

    Game g;
    bool running = true;
    Uint32 lastTick = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) running = false;
                else if (k == SDLK_p) { if (!g.over) g.paused = !g.paused; }
                else if (k == SDLK_r) { if (g.over) g.reset(); }
                else if (!g.paused && !g.over && g.ship.alive) {
                    if (k == SDLK_SPACE) g.fire();
                    else if (k == SDLK_h) g.hyperspace();
                }
            }
        }

        // Continuous input — held keys for movement.
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
        }

        render(ren, g);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}