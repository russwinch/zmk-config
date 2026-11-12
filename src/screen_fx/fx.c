/*
 * Animations for the Prospector dongle's screen: see fx.h.
 *
 * Each animation is a start function (palette, state) and a frame function.
 * `canvas` is one spare picture they share for whatever has to survive from
 * frame to frame (flame heat, fading trails); the frame handed in is redrawn
 * completely every time, because the status text is written over it.
 */

#include "fx.h"

#include <math.h>
#include <string.h>

#define PIXELS (FX_W * FX_H)
#define TWO_PI 6.2831853f

static uint8_t canvas[PIXELS];

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

static int8_t sin8[256];

static void init_tables(void) {
    static bool done;
    if (done) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        sin8[i] = (int8_t)(sinf(i * TWO_PI / 256.0f) * 127.0f);
    }
    done = true;
}

static inline void plot(uint8_t *buf, int x, int y, uint8_t c) {
    if ((unsigned)x < FX_W && (unsigned)y < FX_H) {
        buf[y * FX_W + x] = c;
    }
}

static void line(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 400; guard++) {
        plot(buf, x0, y0, c);
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

/* Darken the whole canvas by `amount` palette steps. */
static void fade_canvas(int amount) {
    for (int i = 0; i < PIXELS; i++) {
        int v = canvas[i] - amount;
        canvas[i] = v > 0 ? v : 0;
    }
}

/* Where across the screen a key lands: its column on a 10-column board. */
static int key_x(uint8_t key) { return 8 + (key % 10) * 13 + (int)(rnd() % 7); }

/* --- warp: a starfield that goes to light speed as you type ---------------- */

#define STARS 110

static struct {
    float x, y, z;
} stars[STARS];
static float warp_kick;

static void star_spawn(int i, float z) {
    stars[i].x = rndf();
    stars[i].y = rndf();
    stars[i].z = z;
}

static void warp_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0}, {90, 30, 50, 140}, {200, 170, 200, 255}, {251, 255, 255, 255}};
    gradient(palette, stops, 4);
    for (int i = 0; i < STARS; i++) {
        star_spawn(i, 0.05f + (rnd() & 1023) / 1080.0f);
    }
    warp_kick = 0;
}

static void warp_frame(uint8_t *fb, const struct fx_input *in) {
    warp_kick = warp_kick * 0.85f + in->n_keys * 0.012f;
    float speed = (0.003f + in->energy * 0.045f + warp_kick) * in->step;

    fade_canvas(30 + (int)(60 * (1.0f - in->energy)));

    for (int i = 0; i < STARS; i++) {
        float z0 = stars[i].z;
        float z1 = z0 - speed;
        if (z1 < 0.03f) {
            star_spawn(i, 1.0f);
            continue;
        }
        int x0 = FX_W / 2 + (int)(stars[i].x / z0 * (FX_W / 2));
        int y0 = FX_H / 2 + (int)(stars[i].y / z0 * (FX_H / 2));
        int x1 = FX_W / 2 + (int)(stars[i].x / z1 * (FX_W / 2));
        int y1 = FX_H / 2 + (int)(stars[i].y / z1 * (FX_H / 2));
        stars[i].z = z1;
        if ((unsigned)x1 >= FX_W || (unsigned)y1 >= FX_H) {
            star_spawn(i, 1.0f);
            continue;
        }
        line(canvas, x0, y0, x1, y1, (uint8_t)(60 + (1.0f - z1) * 191));
    }
    memcpy(fb, canvas, PIXELS);
}

/* --- fire: embers when idle, a wall of flame at full speed ----------------- */

static void fire_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0},        {40, 70, 0, 0},      {100, 220, 30, 0},
                                        {160, 255, 140, 0},  {215, 255, 230, 60}, {251, 255, 255, 230}};
    gradient(palette, stops, 6);
}

static void fire_frame(uint8_t *fb, const struct fx_input *in) {
    /* the fuel: the bottom three rows */
    int base = 110 + (int)(in->energy * 141);
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
    /* flames cool fast when idle (they die low), slowly at full speed */
    uint32_t cooling = 5 + (uint32_t)((1.0f - in->energy) * 9);
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

/* --- plasma: slow colour soup that speeds up and tightens ------------------ */

static float plasma_t;
static int plasma_shift;

static void plasma_start(uint16_t *palette) {
    /* a loop of colour with dark bands in it, so it has some shape */
    for (int i = 0; i < FX_COLORS; i++) {
        int phase = i * 256 / FX_COLORS;
        palette[i] = hue_rgb(phase, 150 + (sin8[(phase * 3) & 255] * 105) / 127);
    }
    plasma_t = 0;
    plasma_shift = 0;
}

static void plasma_frame(uint8_t *fb, const struct fx_input *in) {
    plasma_t += in->step * (0.5f + in->energy * 5.0f);
    plasma_shift += in->n_keys * 20;
    int t = (int)plasma_t;
    int zoom = 2 + (int)(in->energy * 3);

    /* everything that depends on only x or only y, worked out once per frame */
    static int16_t col_a[FX_W], col_b[FX_W], col_c[FX_W];
    static int16_t row_a[FX_H], row_b[FX_H], row_c[FX_H];
    int cx = FX_W / 2 + sin8[(t / 2) & 255] / 3, cy = FX_H / 2 + sin8[(t / 3 + 64) & 255] / 3;
    for (int x = 0; x < FX_W; x++) {
        col_a[x] = sin8[(x * zoom + t) & 255];
        col_b[x] = x * (zoom + 1) + sin8[(x + t * 2) & 255] / 4;
        col_c[x] = (x - cx) * (x - cx) / 6;
    }
    for (int y = 0; y < FX_H; y++) {
        row_a[y] = sin8[(y * (zoom + 1) - t * 2) & 255];
        row_b[y] = y * zoom - t + sin8[(y * 2 - t) & 255] / 4;
        row_c[y] = (y - cy) * (y - cy) / 6 - t * 3;
    }

    int shift = plasma_shift + t * 2;
    uint8_t *p = fb;
    for (int y = 0; y < FX_H; y++) {
        for (int x = 0; x < FX_W; x++) {
            int v = col_a[x] + row_a[y] + sin8[(col_b[x] + row_b[y]) & 255] +
                    sin8[(col_c[x] + row_c[y]) & 255];
            *p++ = (uint8_t)((((v >> 1) + shift) & 255) * FX_COLORS >> 8);
        }
    }
}

/* --- rain: falling code, one new drop for every key ------------------------ */

#define RAIN_COLS 28 /* 5 pixels each */
#define RAIN_ROWS 20 /* 6 pixels each */

static uint8_t rain_lum[RAIN_COLS][RAIN_ROWS];
static uint32_t rain_glyph[RAIN_COLS][RAIN_ROWS]; /* 4x5 dots */
static float rain_head[RAIN_COLS];                /* row of the falling head, <0: none */
static float rain_speed[RAIN_COLS];

static void rain_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0}, {120, 0, 110, 30}, {225, 40, 255, 90}, {251, 220, 255, 230}};
    gradient(palette, stops, 4);
    memset(rain_lum, 0, sizeof(rain_lum));
    for (int c = 0; c < RAIN_COLS; c++) {
        rain_head[c] = -1;
    }
}

static void rain_drop(int c) {
    if (rain_head[c] < 0) {
        rain_head[c] = 0;
        rain_speed[c] = 0.25f + (rnd() & 255) / 600.0f;
    }
}

static void rain_frame(uint8_t *fb, const struct fx_input *in) {
    for (int k = 0; k < in->n_keys; k++) {
        rain_drop(key_x(in->keys[k]) / 5);
    }
    /* background drizzle, a downpour at speed */
    uint32_t chance = (uint32_t)((0.004f + in->energy * 0.06f) * in->step * 65536);
    float fall = in->step * (0.6f + in->energy * 2.2f);
    int decay = 6 + (int)(in->step * 5);

    memset(fb, 0, PIXELS);
    for (int c = 0; c < RAIN_COLS; c++) {
        if ((rnd() & 0xffff) < chance) {
            rain_drop(c);
        }
        if (rain_head[c] >= 0) {
            int from = (int)rain_head[c];
            rain_head[c] += rain_speed[c] * fall;
            for (int r = from; r <= (int)rain_head[c] && r < RAIN_ROWS; r++) {
                rain_lum[c][r] = 251;
                rain_glyph[c][r] = rnd() | (1u << (rnd() % 20)); /* never empty */
            }
            if (rain_head[c] >= RAIN_ROWS) {
                rain_head[c] = -1;
            }
        }
        for (int r = 0; r < RAIN_ROWS; r++) {
            int lum = rain_lum[c][r];
            if (lum == 0) {
                continue;
            }
            if ((rnd() & 63) == 0) {
                rain_glyph[c][r] ^= 1u << (rnd() % 20); /* the code flickers */
            }
            uint32_t bits = rain_glyph[c][r];
            uint8_t *cell = &fb[r * 6 * FX_W + c * 5];
            for (int y = 0; y < 5; y++, cell += FX_W) {
                for (int x = 0; x < 4; x++, bits >>= 1) {
                    if (bits & 1) {
                        cell[x] = (uint8_t)lum;
                    }
                }
            }
            /* the head is white for a moment, then drops into the green */
            lum = lum > 225 ? lum - 13 : lum - decay;
            rain_lum[c][r] = lum > 0 ? lum : 0;
        }
    }
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

/* --- wire: a spinning icosahedron that winds up as you type ---------------- */

#define WIRE_VERTS 12
#define WIRE_EDGES 30
#define PHI 1.618034f

static const float wire_verts[WIRE_VERTS][3] = {
    {0, 1, PHI},  {0, -1, PHI},  {0, 1, -PHI},  {0, -1, -PHI}, {1, PHI, 0},  {-1, PHI, 0},
    {1, -PHI, 0}, {-1, -PHI, 0}, {PHI, 0, 1},   {-PHI, 0, 1},  {PHI, 0, -1}, {-PHI, 0, -1},
};
static uint8_t wire_edges[WIRE_EDGES][2];
static float wire_a, wire_b, wire_c, wire_pulse;

static void wire_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0}, {80, 70, 0, 120}, {180, 0, 190, 255}, {251, 240, 255, 255}};
    gradient(palette, stops, 4);

    /* edges join the vertices that are 2 apart */
    int n = 0;
    for (int i = 0; i < WIRE_VERTS; i++) {
        for (int j = i + 1; j < WIRE_VERTS && n < WIRE_EDGES; j++) {
            float dx = wire_verts[i][0] - wire_verts[j][0], dy = wire_verts[i][1] - wire_verts[j][1],
                  dz = wire_verts[i][2] - wire_verts[j][2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 3.9f && d2 < 4.1f) {
                wire_edges[n][0] = i;
                wire_edges[n][1] = j;
                n++;
            }
        }
    }
    wire_a = wire_b = wire_c = wire_pulse = 0;
}

static void wire_frame(uint8_t *fb, const struct fx_input *in) {
    float spin = (0.012f + in->energy * 0.16f) * in->step;
    wire_a += spin;
    wire_b += spin * 0.71f;
    wire_c += spin * 0.37f;
    wire_pulse = wire_pulse * 0.8f + in->n_keys * 0.12f;
    if (wire_pulse > 0.5f) {
        wire_pulse = 0.5f;
    }

    float sa = sinf(wire_a), ca = cosf(wire_a), sb = sinf(wire_b), cb = cosf(wire_b);
    float sc = sinf(wire_c), cc = cosf(wire_c);
    float size = 21.0f * (1.0f + wire_pulse + in->energy * 0.25f);

    int px[WIRE_VERTS], py[WIRE_VERTS];
    float pz[WIRE_VERTS];
    for (int i = 0; i < WIRE_VERTS; i++) {
        float x = wire_verts[i][0], y = wire_verts[i][1], z = wire_verts[i][2];
        float t = x * ca - y * sa; /* about z */
        y = x * sa + y * ca;
        x = t;
        t = x * cb + z * sb; /* about y */
        z = -x * sb + z * cb;
        x = t;
        t = y * cc - z * sc; /* about x */
        z = y * sc + z * cc;
        y = t;
        float depth = 5.0f / (5.0f + z);
        px[i] = FX_W / 2 + (int)(x * size * depth);
        py[i] = FX_H / 2 + (int)(y * size * depth);
        pz[i] = z;
    }

    /* long trails when spinning fast */
    fade_canvas(70 - (int)(in->energy * 45));
    for (int e = 0; e < WIRE_EDGES; e++) {
        int a = wire_edges[e][0], b = wire_edges[e][1];
        /* nearer edges are brighter (z runs about -1.9..1.9, negative is near) */
        float closeness = clampf(0.5f - (pz[a] + pz[b]) / 7.6f, 0.0f, 1.0f);
        line(canvas, px[a], py[a], px[b], py[b], (uint8_t)(110 + closeness * 141));
    }
    memcpy(fb, canvas, PIXELS);
}

/* --- the list --------------------------------------------------------------- */

static const struct {
    const char *name;
    void (*start)(uint16_t *palette);
    void (*frame)(uint8_t *fb, const struct fx_input *in);
} effects[] = {
    {"WARP", warp_start, warp_frame},       {"FIRE", fire_start, fire_frame},
    {"PLASMA", plasma_start, plasma_frame}, {"RAIN", rain_start, rain_frame},
    {"SPARKS", sparks_start, sparks_frame}, {"WIRE", wire_start, wire_frame},
};

#define FX_COUNT ((int)(sizeof(effects) / sizeof(effects[0])))

int fx_count(void) { return FX_COUNT; }

const char *fx_name(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].name : ""; }

void fx_start(int fx, uint16_t *palette) {
    if (fx < 0 || fx >= FX_COUNT) {
        return;
    }
    init_tables();
    memset(canvas, 0, sizeof(canvas));
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
    /* kept clear of the panel's rounded corners */
    const int y = FX_H - 16;
    int x = battery_text(fb, 10, y, 'L', status->battery[0], status->half_connected[0]);
    battery_text(fb, x + 5, y, 'R', status->battery[1], status->half_connected[1]);

    int len = (int)strlen(status->host);
    text(fb, FX_W - 10 - 6 * len + 1, y, status->host, status->host_connected ? FX_WHITE : FX_RED);

    if (banner) {
        text(fb, 10, 9, banner, FX_WHITE);
    }
}
