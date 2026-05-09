// Asteroids for the terminal
// Compile: g++ -std=c++17 -O2 asteroids.cpp -lncurses -o asteroids
// Run:     ./asteroids
//
// Controls:
//   Left / Right arrows : rotate
//   Up arrow            : thrust
//   Space               : fire
//   H                   : hyperspace (random teleport, risky)
//   P                   : pause
//   R                   : restart after game over
//   Q                   : quit

#include <ncurses.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

constexpr double PI = 3.14159265358979323846;

// World units are character cells. Terminal cells are ~2x taller than wide,
// so we treat horizontal movement at 2x speed to keep motion visually even.
constexpr int   WORLD_W = 80;
constexpr int   WORLD_H = 24;
constexpr double SHIP_THRUST     = 0.05;
constexpr double SHIP_TURN_RATE  = 0.20;   // radians per frame
constexpr double SHIP_FRICTION   = 0.995;
constexpr double SHIP_MAX_SPEED  = 1.2;
constexpr double BULLET_SPEED    = 1.5;
constexpr int    BULLET_LIFE     = 40;     // frames
constexpr int    FIRE_COOLDOWN   = 6;      // frames between shots
constexpr int    RESPAWN_INVULN  = 90;     // frames of invulnerability after respawn

static double frand() { return (double)std::rand() / RAND_MAX; }

static double wrapX(double x) {
    while (x < 0) x += WORLD_W;
    while (x >= WORLD_W) x -= WORLD_W;
    return x;
}
static double wrapY(double y) {
    while (y < 0) y += WORLD_H;
    while (y >= WORLD_H) y -= WORLD_H;
    return y;
}

// Toroidal distance — shortest distance accounting for screen wrap.
static double wrapDist(double x1, double y1, double x2, double y2) {
    double dx = std::fabs(x1 - x2);
    double dy = std::fabs(y1 - y2);
    if (dx > WORLD_W / 2.0) dx = WORLD_W - dx;
    if (dy > WORLD_H / 2.0) dy = WORLD_H - dy;
    return std::sqrt(dx * dx + dy * dy);
}

struct Ship {
    double x, y;
    double vx = 0, vy = 0;
    double angle = -PI / 2; // pointing up
    bool alive = true;
    int invuln = RESPAWN_INVULN;
    int cooldown = 0;
};

struct Bullet {
    double x, y;
    double vx, vy;
    int life;
};

struct Asteroid {
    double x, y;
    double vx, vy;
    int size; // 3 = large, 2 = medium, 1 = small
};

struct Game {
    Ship ship;
    std::vector<Bullet> bullets;
    std::vector<Asteroid> asteroids;
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
        score = 0;
        lives = 3;
        level = 1;
        over = false;
        paused = false;
        bullets.clear();
        spawnShip();
        startLevel();
    }

    void spawnShip() {
        ship.x = WORLD_W / 2.0;
        ship.y = WORLD_H / 2.0;
        ship.vx = ship.vy = 0;
        ship.angle = -PI / 2;
        ship.alive = true;
        ship.invuln = RESPAWN_INVULN;
        ship.cooldown = 0;
    }

    void startLevel() {
        asteroids.clear();
        int count = 3 + level;
        for (int i = 0; i < count; i++) {
            Asteroid a;
            // Spawn away from the ship so the player isn't insta-killed.
            do {
                a.x = frand() * WORLD_W;
                a.y = frand() * WORLD_H;
            } while (wrapDist(a.x, a.y, ship.x, ship.y) < 15);
            double ang = frand() * 2 * PI;
            double speed = 0.15 + frand() * 0.25;
            a.vx = std::cos(ang) * speed * 2.0; // x doubled to compensate cell aspect
            a.vy = std::sin(ang) * speed;
            a.size = 3;
            asteroids.push_back(a);
        }
    }

    int asteroidRadius(int size) const {
        // Visual radius in cells (in y units; x scales by 2 in collision math)
        if (size == 3) return 3;
        if (size == 2) return 2;
        return 1;
    }

    int asteroidPoints(int size) const {
        if (size == 3) return 20;
        if (size == 2) return 50;
        return 100;
    }

    void rotate(double d) { ship.angle += d; }

    void thrust() {
        ship.vx += std::cos(ship.angle) * SHIP_THRUST * 2.0;
        ship.vy += std::sin(ship.angle) * SHIP_THRUST;
        double sp = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy);
        if (sp > SHIP_MAX_SPEED * 2.0) {
            ship.vx *= (SHIP_MAX_SPEED * 2.0) / sp;
            ship.vy *= (SHIP_MAX_SPEED * 2.0) / sp;
        }
    }

    void fire() {
        if (!ship.alive || ship.cooldown > 0) return;
        Bullet b;
        b.x = ship.x + std::cos(ship.angle) * 1.5 * 2.0;
        b.y = ship.y + std::sin(ship.angle) * 1.5;
        b.vx = std::cos(ship.angle) * BULLET_SPEED * 2.0;
        b.vy = std::sin(ship.angle) * BULLET_SPEED;
        b.life = BULLET_LIFE;
        bullets.push_back(b);
        ship.cooldown = FIRE_COOLDOWN;
    }

    void hyperspace() {
        if (!ship.alive) return;
        ship.x = frand() * WORLD_W;
        ship.y = frand() * WORLD_H;
        ship.vx = ship.vy = 0;
        // Classic Asteroids: hyperspace has a small chance of disaster.
        if (frand() < 0.10) killShip();
    }

    void killShip() {
        if (!ship.alive || ship.invuln > 0) return;
        ship.alive = false;
        lives--;
        if (lives <= 0) {
            over = true;
        }
    }

    void splitAsteroid(size_t i) {
        Asteroid a = asteroids[i];
        score += asteroidPoints(a.size);
        asteroids.erase(asteroids.begin() + i);
        if (a.size > 1) {
            for (int k = 0; k < 2; k++) {
                Asteroid n;
                n.x = a.x;
                n.y = a.y;
                double ang = frand() * 2 * PI;
                double speed = 0.25 + frand() * 0.35;
                n.vx = std::cos(ang) * speed * 2.0;
                n.vy = std::sin(ang) * speed;
                n.size = a.size - 1;
                asteroids.push_back(n);
            }
        }
    }

    void update() {
        if (over || paused) return;

        // Ship
        if (ship.alive) {
            ship.vx *= SHIP_FRICTION;
            ship.vy *= SHIP_FRICTION;
            ship.x = wrapX(ship.x + ship.vx);
            ship.y = wrapY(ship.y + ship.vy);
            if (ship.cooldown > 0) ship.cooldown--;
            if (ship.invuln > 0) ship.invuln--;
        } else {
            // Wait until clear, then respawn.
            bool safe = true;
            double cx = WORLD_W / 2.0, cy = WORLD_H / 2.0;
            for (auto& a : asteroids) {
                if (wrapDist(cx, cy, a.x, a.y) < 8) { safe = false; break; }
            }
            if (safe && lives > 0) spawnShip();
        }

        // Bullets
        for (auto it = bullets.begin(); it != bullets.end(); ) {
            it->x = wrapX(it->x + it->vx);
            it->y = wrapY(it->y + it->vy);
            it->life--;
            if (it->life <= 0) it = bullets.erase(it);
            else ++it;
        }

        // Asteroids
        for (auto& a : asteroids) {
            a.x = wrapX(a.x + a.vx);
            a.y = wrapY(a.y + a.vy);
        }

        // Bullet vs asteroid
        for (size_t i = 0; i < bullets.size(); ) {
            bool hit = false;
            for (size_t j = 0; j < asteroids.size(); j++) {
                double r = asteroidRadius(asteroids[j].size);
                if (wrapDist(bullets[i].x, bullets[i].y,
                             asteroids[j].x, asteroids[j].y) < r + 0.5) {
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
                double r = asteroidRadius(a.size);
                if (wrapDist(ship.x, ship.y, a.x, a.y) < r + 0.8) {
                    killShip();
                    break;
                }
            }
        }

        // Level clear
        if (asteroids.empty()) {
            level++;
            startLevel();
        }
    }
};

// Convert world (x, y) to screen cell. World x is in 0..WORLD_W; we render
// 1:1 to character cells. Returns true if onscreen.
static bool toScreen(double x, double y, int& sy, int& sx, int originY, int originX) {
    int ix = (int)std::round(x);
    int iy = (int)std::round(y);
    if (ix < 0 || ix >= WORLD_W || iy < 0 || iy >= WORLD_H) return false;
    sy = originY + iy;
    sx = originX + ix;
    return true;
}

// Pick a ship glyph based on facing angle (8-way).
static const char* shipGlyph(double angle) {
    double a = angle;
    while (a < 0) a += 2 * PI;
    while (a >= 2 * PI) a -= 2 * PI;
    int oct = (int)std::round(a / (PI / 4)) % 8;
    switch (oct) {
        case 0: return ">"; // right
        case 1: return "\\"; // down-right
        case 2: return "v"; // down
        case 3: return "/"; // down-left  (rendered as /)
        case 4: return "<"; // left
        case 5: return "\\"; // up-left   (rendered as \)
        case 6: return "^"; // up
        case 7: return "/"; // up-right
    }
    return "^";
}

static void render(const Game& g) {
    erase();

    int originY = 1;
    int originX = 1;

    // Border
    for (int x = 0; x < WORLD_W + 2; x++) {
        mvaddch(originY - 1,         originX - 1 + x, '-');
        mvaddch(originY + WORLD_H,   originX - 1 + x, '-');
    }
    for (int y = 0; y < WORLD_H; y++) {
        mvaddch(originY + y, originX - 1,         '|');
        mvaddch(originY + y, originX + WORLD_W,   '|');
    }
    mvaddch(originY - 1,       originX - 1,       '+');
    mvaddch(originY - 1,       originX + WORLD_W, '+');
    mvaddch(originY + WORLD_H, originX - 1,       '+');
    mvaddch(originY + WORLD_H, originX + WORLD_W, '+');

    // Asteroids
    attron(COLOR_PAIR(3));
    for (auto& a : g.asteroids) {
        int sy, sx;
        if (toScreen(a.x, a.y, sy, sx, originY, originX)) {
            const char* glyph;
            if (a.size == 3)      glyph = "@";
            else if (a.size == 2) glyph = "O";
            else                  glyph = "o";
            mvaddstr(sy, sx, glyph);
        }
    }
    attroff(COLOR_PAIR(3));

    // Bullets
    attron(COLOR_PAIR(2));
    for (auto& b : g.bullets) {
        int sy, sx;
        if (toScreen(b.x, b.y, sy, sx, originY, originX)) {
            mvaddch(sy, sx, '.');
        }
    }
    attroff(COLOR_PAIR(2));

    // Ship (blink while invulnerable)
    if (g.ship.alive) {
        bool show = g.ship.invuln == 0 || (g.ship.invuln / 4) % 2 == 0;
        if (show) {
            int sy, sx;
            if (toScreen(g.ship.x, g.ship.y, sy, sx, originY, originX)) {
                attron(COLOR_PAIR(1));
                mvaddstr(sy, sx, shipGlyph(g.ship.angle));
                attroff(COLOR_PAIR(1));
            }
        }
    }

    // HUD
    int panelY = originY + WORLD_H + 1;
    mvprintw(panelY,     originX, "ASTEROIDS  Score: %-6d  Lives: %d  Level: %d",
             g.score, g.lives, g.level);
    mvprintw(panelY + 1, originX,
             "Controls: <- -> rotate | up thrust | space fire | h hyperspace | p pause | r restart | q quit");

    if (g.paused) {
        mvprintw(originY + WORLD_H / 2, originX + WORLD_W / 2 - 4, " PAUSED ");
    }
    if (g.over) {
        mvprintw(originY + WORLD_H / 2,     originX + WORLD_W / 2 - 5, " GAME OVER ");
        mvprintw(originY + WORLD_H / 2 + 1, originX + WORLD_W / 2 - 8, " press r to retry ");
    }

    refresh();
}

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN,   -1); // ship
        init_pair(2, COLOR_YELLOW, -1); // bullets
        init_pair(3, COLOR_WHITE,  -1); // asteroids
    }

    Game g;
    auto last = std::chrono::steady_clock::now();
    constexpr int FRAME_MS = 33; // ~30 FPS

    while (true) {
        // Drain all queued input each frame so held keys feel responsive.
        int ch;
        while ((ch = getch()) != ERR) {
            if (ch == 'q' || ch == 'Q') { endwin(); return 0; }
            if (ch == 'p' || ch == 'P') { if (!g.over) g.paused = !g.paused; continue; }
            if (ch == 'r' || ch == 'R') { if (g.over) g.reset(); continue; }
            if (g.paused || g.over || !g.ship.alive) continue;
            switch (ch) {
                case KEY_LEFT:  g.rotate(-SHIP_TURN_RATE); break;
                case KEY_RIGHT: g.rotate( SHIP_TURN_RATE); break;
                case KEY_UP:    g.thrust(); break;
                case ' ':       g.fire(); break;
                case 'h': case 'H': g.hyperspace(); break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (elapsed >= FRAME_MS) {
            g.update();
            last = now;
        }

        render(g);
        napms(8);
    }

    endwin();
    return 0;
}