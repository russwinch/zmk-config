/*
 * Animations for the Prospector dongle's screen: see fx.h.
 *
 * Each animation is a start function (palette, state) and a frame function.
 * `canvas` is one spare picture they share for whatever has to survive from
 * frame to frame (flame heat, fading trails); the frame handed in is redrawn
 * completely every time, because the status text is written over it. The
 * caller owns the canvas's memory (fx_set_canvas): the dongle has none to
 * spare for it.
 */

#include "fx.h"

#include <math.h>
#include <string.h>

#define PIXELS (FX_W * FX_H)
#define TWO_PI 6.2831853f

static uint8_t *canvas;

void fx_set_canvas(uint8_t *memory) { canvas = memory; }

/* --- helpers -------------------------------------------------------------- */

static uint32_t rng_state = 0x2545f491;

static inline uint32_t rnd(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

/* -1..1 */
static inline float rndf(void) { return (int32_t)(rnd() & 0xffff) / 32768.0f - 1.0f; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline uint16_t rgb(int r, int g, int b) {
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

struct stop {
    uint8_t at, r, g, b;
};

/* Fill palette entries with a gradient through `stops` (ascending `at`). */
static void gradient(uint16_t *palette, const struct stop *stops, int n) {
    for (int s = 0; s < n - 1; s++) {
        const struct stop *a = &stops[s], *b = &stops[s + 1];
        int span = b->at - a->at;
        for (int i = 0; i <= span; i++) {
            palette[a->at + i] = rgb(a->r + (b->r - a->r) * i / span, a->g + (b->g - a->g) * i / span,
                                     a->b + (b->b - a->b) * i / span);
        }
    }
}

/* hue 0..255, fully saturated, brightness 0..255 */
static uint16_t hue_rgb(int hue, int value) {
    int h = (hue & 255) * 6;
    int f = h & 255, r, g, b;
    switch (h >> 8) {
    case 0: r = 255, g = f, b = 0; break;
    case 1: r = 255 - f, g = 255, b = 0; break;
    case 2: r = 0, g = 255, b = f; break;
    case 3: r = 0, g = 255 - f, b = 255; break;
    case 4: r = f, g = 0, b = 255; break;
    default: r = 255, g = 0, b = 255 - f; break;
    }
    return rgb(r * value / 255, g * value / 255, b * value / 255);
}

static inline void plot(uint8_t *buf, int x, int y, uint8_t c) {
    if ((unsigned)x < FX_W && (unsigned)y < FX_H) {
        buf[y * FX_W + x] = c;
    }
}

/* A line; with `under`, only where the picture is still black. */
static void line_on(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t c, bool under) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 400; guard++) {
        if ((unsigned)x0 < FX_W && (unsigned)y0 < FX_H && !(under && buf[y0 * FX_W + x0])) {
            buf[y0 * FX_W + x0] = c;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void line(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t c) {
    line_on(buf, x0, y0, x1, y1, c, false);
}

/* Where a key is on the board, taking it for 10 columns by 4 rows (the
 * Waterfowl; near enough on the others). */
static inline int key_col(uint8_t key) { return key % 10; }
static inline int key_row(uint8_t key) { return key / 10 < 3 ? key / 10 : 3; }

/* Where across the screen a key lands: its column. */
static int key_x(uint8_t key) { return 8 + key_col(key) * 13 + (int)(rnd() % 7); }

/* --- warp: a starfield you fly through, steered by where you type ---------- */

/*
 * Stars come out of a vanishing point that wanders, and leans towards the
 * side of the board being typed on. Each key fires a few coloured comets out
 * through that key's place on the screen (left hand cyan, right hand amber,
 * thumbs magenta). At speed, the rings of a tunnel come past, in the colour
 * of the last key.
 */

#define STARS 110
#define COMETS_PER_KEY 4
#define WARP_RINGS 8
#define WARP_LEVELS 63    /* palette: 4 colours x 63 brightness levels */
#define WARP_SCALE 70.0f  /* pixels from the vanishing point, for 1 across at depth 1 */
#define RING_FAR 2.0f     /* depth a ring appears at */
#define RING_RADIUS 0.55f /* 38 pixels at depth 1 */

static struct {
    float x, y, z;
    uint8_t hue, comet;
} stars[STARS];
static struct {
    float z, turn; /* z <= 0: not in use */
    uint8_t hue;
} rings[WARP_RINGS];
static float warp_kick, warp_time, warp_ring_gap;
static float warp_cx, warp_cy;       /* the vanishing point */
static float warp_lean_x, warp_lean_y; /* -1..1: where the typing is */
static int warp_next, warp_hue;

static void star_spawn(int i, float z) {
    stars[i].x = rndf();
    stars[i].y = rndf();
    stars[i].z = z;
    stars[i].hue = 0;
    stars[i].comet = 0;
}

static void warp_start(uint16_t *palette) {
    static const struct stop stops[] = {
        {0, 0, 0, 0},   {22, 30, 50, 140},   {50, 170, 200, 255},  {62, 255, 255, 255}, /* stars */
        {63, 0, 0, 0},  {85, 0, 80, 110},    {113, 40, 240, 230},  {125, 220, 255, 255},
        {126, 0, 0, 0}, {148, 130, 45, 0},   {176, 255, 180, 30},  {188, 255, 250, 210},
        {189, 0, 0, 0}, {211, 110, 0, 120},  {239, 255, 70, 220},  {251, 255, 225, 255},
    };
    for (int h = 0; h < 4; h++) {
        gradient(palette, &stops[h * 4], 4);
    }
    for (int i = 0; i < STARS; i++) {
        star_spawn(i, 0.05f + (rnd() & 1023) / 1080.0f);
    }
    memset(rings, 0, sizeof(rings));
    warp_kick = warp_time = warp_ring_gap = 0;
    warp_cx = FX_W / 2;
    warp_cy = FX_H / 2;
    warp_lean_x = warp_lean_y = 0;
    warp_hue = 0;
}

static void warp_key(uint8_t key) {
    /* the key's place, as a direction from the vanishing point */
    float tx = (key_col(key) - 4.5f) / 4.5f, ty = (key_row(key) - 1.5f) / 1.5f;
    warp_hue = key_row(key) == 3 ? 3 : key_col(key) < 5 ? 1 : 2;
    warp_lean_x = clampf(warp_lean_x + tx * 0.3f, -1.0f, 1.0f);
    warp_lean_y = clampf(warp_lean_y + ty * 0.3f, -1.0f, 1.0f);

    for (int i = 0; i < COMETS_PER_KEY; i++) {
        warp_next = (warp_next + 1) % STARS;
        /* starts close in, and is at the key's place by depth 0.4 */
        stars[warp_next].x = (tx * 0.8f + rndf() * 0.07f) * 0.4f;
        stars[warp_next].y = (ty * 0.6f + rndf() * 0.07f) * 0.4f;
        stars[warp_next].z = 1.6f + rndf() * 0.3f;
        stars[warp_next].hue = warp_hue;
        stars[warp_next].comet = 1;
    }
}

static void warp_frame(uint8_t *fb, const struct fx_input *in) {
    warp_kick = warp_kick * 0.85f + in->n_keys * 0.012f;
    float speed = (0.003f + in->energy * 0.045f + warp_kick) * in->step;
    warp_time += in->step;

    for (int k = 0; k < in->n_keys; k++) {
        warp_key(in->keys[k]);
    }

    /* the vanishing point: a slow wander, plus the lean, which eases back */
    float ease = clampf(0.08f * in->step, 0.0f, 1.0f);
    warp_lean_x -= warp_lean_x * ease * 0.3f;
    warp_lean_y -= warp_lean_y * ease * 0.3f;
    warp_cx += (FX_W / 2 + sinf(warp_time * 0.011f) * 9 + warp_lean_x * 28 - warp_cx) * ease;
    warp_cy += (FX_H / 2 + sinf(warp_time * 0.017f + 1) * 6 + warp_lean_y * 16 - warp_cy) * ease;
    int cx = (int)warp_cx, cy = (int)warp_cy;

    /* trails: short when idle, long at speed. Each colour fades to its own black. */
    int fade = 8 + (int)(15 * (1.0f - in->energy));
    uint8_t faded[FX_COLORS];
    for (int i = 0; i < FX_COLORS; i++) {
        faded[i] = i % WARP_LEVELS > fade ? i - fade : 0;
    }
    for (int i = 0; i < PIXELS; i++) {
        canvas[i] = faded[canvas[i]];
    }

    for (int i = 0; i < STARS; i++) {
        float z0 = stars[i].z;
        /* comets fly even when the stars are only drifting */
        float z1 = z0 - speed - (stars[i].comet ? 0.03f * in->step : 0);
        if (z1 < 0.03f) {
            star_spawn(i, 1.0f);
            continue;
        }
        int x0 = cx + (int)(stars[i].x / z0 * WARP_SCALE), y0 = cy + (int)(stars[i].y / z0 * WARP_SCALE);
        int x1 = cx + (int)(stars[i].x / z1 * WARP_SCALE), y1 = cy + (int)(stars[i].y / z1 * WARP_SCALE);
        stars[i].z = z1;
        if ((unsigned)x1 >= FX_W || (unsigned)y1 >= FX_H) {
            star_spawn(i, 1.0f);
            continue;
        }
        int level = stars[i].comet ? 40 + (int)((RING_FAR - z1) * 14) : 15 + (int)((1.0f - z1) * 47);
        uint8_t c = stars[i].hue * WARP_LEVELS + (level > 62 ? 62 : level);
        line(canvas, x0, y0, x1, y1, c);
        if (stars[i].comet) {
            /* twice as thick */
            line(canvas, x0 + 1, y0, x1 + 1, y1, c);
            line(canvas, x0, y0 + 1, x1, y1 + 1, c);
        }
    }
    memcpy(fb, canvas, PIXELS);

    /* the tunnel: hexagons, each turned a little from the one before. They
     * fade in from a fifth of full speed, and go behind the stars. */
    float show = clampf((in->energy - 0.2f) * 2.5f, 0.0f, 1.0f);
    warp_ring_gap += speed;
    bool spawn = warp_ring_gap > 0.24f;
    if (spawn) {
        warp_ring_gap = 0;
    }
    for (int i = 0; i < WARP_RINGS; i++) {
        if (rings[i].z <= 0) {
            if (spawn && show > 0) {
                rings[i].z = RING_FAR;
                rings[i].turn = warp_time * 0.02f;
                rings[i].hue = warp_hue;
                spawn = false;
            }
            continue;
        }
        rings[i].z -= speed;
        float radius = RING_RADIUS / rings[i].z * WARP_SCALE;
        if (rings[i].z < 0.05f || radius > 115) { /* past the corners */
            rings[i].z = 0;
            continue;
        }
        int level = (int)(show * (3 + (RING_FAR - rings[i].z) * 15));
        if (level < 1) {
            continue;
        }
        uint8_t c = rings[i].hue * WARP_LEVELS + level;
        int px = cx + (int)(cosf(rings[i].turn) * radius), py = cy + (int)(sinf(rings[i].turn) * radius);
        for (int corner = 1; corner <= 6; corner++) {
            float angle = rings[i].turn + corner * (TWO_PI / 6);
            int qx = cx + (int)(cosf(angle) * radius), qy = cy + (int)(sinf(angle) * radius);
            line_on(fb, px, py, qx, qy, c, true);
            px = qx;
            py = qy;
        }
    }
}

/* --- fire: embers when idle, a wall of flame at full speed ----------------- */

static void fire_start(uint16_t *palette) {
    /* white only at the very top, which just the keys' hot spots reach: the
     * flames themselves stop at yellow, so they keep their shape at speed */
    static const struct stop stops[] = {{0, 0, 0, 0},       {40, 70, 0, 0},      {100, 210, 25, 0},
                                        {165, 255, 120, 0}, {225, 255, 215, 40}, {251, 255, 255, 220}};
    gradient(palette, stops, 6);
}

static void fire_frame(uint8_t *fb, const struct fx_input *in) {
    /* the fuel: the bottom three rows */
    int base = 110 + (int)(in->energy * 95);
    for (int i = PIXELS - 3 * FX_W; i < PIXELS; i++) {
        int v = base - (int)(rnd() & 63);
        canvas[i] = v > 0 ? v : 0;
    }
    /* each key throws a hot spot in */
    for (int k = 0; k < in->n_keys; k++) {
        int cx = key_x(in->keys[k]);
        for (int y = FX_H - 7; y < FX_H; y++) {
            for (int x = cx - 3; x <= cx + 3; x++) {
                plot(canvas, x, y, 251);
            }
        }
    }
    /* flames cool fast when idle (they die low), slower at full speed, but
     * still fast enough to leave dark between the tongues */
    uint32_t cooling = 9 + (uint32_t)((1.0f - in->energy) * 5);
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < FX_H - 3; y++) {
            uint8_t *row = &canvas[y * FX_W];
            for (int x = 0; x < FX_W; x++) {
                uint32_t r = rnd();
                /* heat comes from 1..3 rows down, drifting sideways */
                int sx = x + (int)(r & 3) - 1;
                if ((unsigned)sx >= FX_W) {
                    sx = x;
                }
                int v = row[(1 + ((r >> 2) % 3)) * FX_W + sx] - (int)((r >> 8) % cooling);
                row[x] = v > 0 ? v : 0;
            }
        }
    }
    memcpy(fb, canvas, PIXELS);
}

/* --- sparks: a firework for every key --------------------------------------- */

#define SPARKS 200
#define SPARK_HUES 7 /* palette: 7 colours x 32 brightness levels */

static struct {
    float x, y, vx, vy;
    uint8_t life, hue;
} sparks[SPARKS];
static int spark_next;
static float spark_timer;

static void sparks_start(uint16_t *palette) {
    for (int h = 0; h < SPARK_HUES; h++) {
        for (int l = 0; l < 32; l++) {
            int hue = h * 256 / SPARK_HUES + 10;
            /* black, up to the colour, then whitening at the top */
            uint16_t c = hue_rgb(hue, l < 24 ? l * 255 / 23 : 255);
            if (l >= 24) {
                int w = (l - 23) * 28;
                int r = (c >> 8) & 0xf8, g = (c >> 3) & 0xfc, b = (c << 3) & 0xf8;
                c = rgb(r + (255 - r) * w / 255, g + (255 - g) * w / 255, b + (255 - b) * w / 255);
            }
            palette[h * 32 + l] = c;
        }
    }
    memset(sparks, 0, sizeof(sparks));
    spark_timer = 10;
}

static void sparks_burst(int x, int y, int count, float power) {
    int hue = rnd() % SPARK_HUES;
    for (int i = 0; i < count; i++) {
        float angle = (rnd() & 255) * TWO_PI / 256.0f;
        float v = power * (0.3f + (rnd() & 255) / 365.0f);
        spark_next = (spark_next + 1) % SPARKS;
        sparks[spark_next].x = x;
        sparks[spark_next].y = y;
        sparks[spark_next].vx = cosf(angle) * v;
        sparks[spark_next].vy = sinf(angle) * v;
        sparks[spark_next].life = 20 + (rnd() & 15);
        sparks[spark_next].hue = hue;
    }
}

static void sparks_frame(uint8_t *fb, const struct fx_input *in) {
    for (int k = 0; k < in->n_keys; k++) {
        sparks_burst(key_x(in->keys[k]), 20 + (int)(rnd() % 55), 16, 2.2f);
    }
    /* a show of its own too: now and then when idle, constantly at speed */
    spark_timer -= in->step;
    if (spark_timer <= 0) {
        sparks_burst(15 + (int)(rnd() % (FX_W - 30)), 15 + (int)(rnd() % 60), 28, 3.0f);
        spark_timer = 55.0f - in->energy * 45.0f;
    }

    /* trails: every pixel drops a couple of brightness levels */
    for (int i = 0; i < PIXELS; i++) {
        int v = canvas[i];
        canvas[i] = (v & 31) > 2 ? v - 2 : 0;
    }
    for (int i = 0; i < SPARKS; i++) {
        if (sparks[i].life == 0) {
            continue;
        }
        sparks[i].life--;
        sparks[i].vy += 0.07f * in->step;
        sparks[i].vx *= 0.97f;
        sparks[i].vy *= 0.97f;
        float x0 = sparks[i].x, y0 = sparks[i].y;
        sparks[i].x += sparks[i].vx * in->step;
        sparks[i].y += sparks[i].vy * in->step;
        int level = sparks[i].life > 15 ? 31 : 10 + sparks[i].life * 21 / 15;
        line(canvas, (int)x0, (int)y0, (int)sparks[i].x, (int)sparks[i].y, sparks[i].hue * 32 + level);
    }
    memcpy(fb, canvas, PIXELS);
}

/* --- the list --------------------------------------------------------------- */

static const struct {
    const char *name;
    void (*start)(uint16_t *palette);
    void (*frame)(uint8_t *fb, const struct fx_input *in);
} effects[] = {
    {"WARP", warp_start, warp_frame},
    {"FIRE", fire_start, fire_frame},
    {"SPARKS", sparks_start, sparks_frame},
};

#define FX_COUNT ((int)(sizeof(effects) / sizeof(effects[0])))

int fx_count(void) { return FX_COUNT; }

const char *fx_name(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].name : ""; }

void fx_start(int fx, uint16_t *palette) {
    if (fx < 0 || fx >= FX_COUNT) {
        return;
    }
    memset(canvas, 0, PIXELS);
    memset(palette, 0, 256 * sizeof(palette[0]));
    effects[fx].start(palette);
    palette[FX_BLACK] = rgb(0, 0, 0);
    palette[FX_RED] = rgb(255, 60, 40);
    palette[FX_GREY] = rgb(150, 150, 150);
    palette[FX_WHITE] = rgb(255, 255, 255);
}

void fx_frame(int fx, uint8_t *fb, const struct fx_input *in) {
    if (fx >= 0 && fx < FX_COUNT) {
        effects[fx].frame(fb, in);
    }
}

/* --- status text ------------------------------------------------------------ */

/* 5x7 dots, top row first, left dot in bit 4. Only the characters used. */
static const struct {
    char c;
    uint8_t rows[7];
} font[] = {
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {'-', {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}},
    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'I', {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
};

static void glyph(uint8_t *fb, int x, int y, char c, uint8_t color) {
    for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].c != c) {
            continue;
        }
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (font[i].rows[row] & (0x10 >> col)) {
                    plot(fb, x + col, y + row, color);
                }
            }
        }
        return;
    }
}

/* Text with a black edge, so it reads over anything. Returns the next x. */
static int text(uint8_t *fb, int x, int y, const char *s, uint8_t color) {
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (const char *p = s; *p; p++, cx += 6) {
            if (pass == 0) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        glyph(fb, cx + dx, y + dy, *p, FX_BLACK);
                    }
                }
            } else {
                glyph(fb, cx, y, *p, color);
            }
        }
    }
    return x + 6 * (int)strlen(s);
}

static int battery_text(uint8_t *fb, int x, int y, char side, uint8_t percent, bool connected) {
    char buf[5] = {side, '-', '-', 0, 0};
    if (connected && percent > 0) {
        if (percent > 99) {
            percent = 99; /* two digits: "99" reads as full */
        }
        buf[1] = '0' + percent / 10;
        buf[2] = '0' + percent % 10;
    }
    return text(fb, x, y, buf, !connected || (percent > 0 && percent <= 15) ? FX_RED : FX_WHITE);
}

void fx_draw_status(uint8_t *fb, const struct fx_status *status, const char *banner) {
    if (status) {
        /* kept clear of the panel's rounded corners */
        const int y = FX_H - 16;
        int x = battery_text(fb, 10, y, 'L', status->battery[0], status->half_connected[0]);
        battery_text(fb, x + 5, y, 'R', status->battery[1], status->half_connected[1]);

        int len = (int)strlen(status->host);
        text(fb, FX_W - 10 - 6 * len + 1, y, status->host, status->host_connected ? FX_WHITE : FX_RED);
    }

    if (banner) {
        text(fb, 10, 9, banner, FX_WHITE);
    }
}
