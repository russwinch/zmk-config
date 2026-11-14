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

/* --- text ------------------------------------------------------------------ */

/* X11's "5x7" (misc-fixed, public domain), ' ' to '~': top row first, left
 * dot in bit 4. Characters are 5 apart, the gap included. */
static const uint8_t font[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*   */
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00}, /* ! */
    {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x00, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x00}, /* # */
    {0x00, 0x0e, 0x14, 0x0e, 0x05, 0x0e, 0x00}, /* $ */
    {0x10, 0x12, 0x04, 0x08, 0x12, 0x02, 0x00}, /* % */
    {0x00, 0x08, 0x14, 0x08, 0x14, 0x0a, 0x00}, /* & */
    {0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x04, 0x08, 0x08, 0x08, 0x08, 0x04, 0x00}, /* ( */
    {0x08, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00}, /* ) */
    {0x00, 0x0a, 0x04, 0x0e, 0x04, 0x0a, 0x00}, /* * */
    {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}, /* , */
    {0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x00}, /* . */
    {0x00, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, /* / */
    {0x04, 0x0a, 0x0a, 0x0a, 0x0a, 0x04, 0x00}, /* 0 */
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x0e, 0x00}, /* 1 */
    {0x0c, 0x12, 0x02, 0x04, 0x08, 0x1e, 0x00}, /* 2 */
    {0x1e, 0x02, 0x0c, 0x02, 0x12, 0x0c, 0x00}, /* 3 */
    {0x04, 0x0c, 0x14, 0x1e, 0x04, 0x04, 0x00}, /* 4 */
    {0x1e, 0x10, 0x1c, 0x02, 0x12, 0x0c, 0x00}, /* 5 */
    {0x0c, 0x10, 0x1c, 0x12, 0x12, 0x0c, 0x00}, /* 6 */
    {0x1e, 0x02, 0x04, 0x04, 0x08, 0x08, 0x00}, /* 7 */
    {0x0c, 0x12, 0x0c, 0x12, 0x12, 0x0c, 0x00}, /* 8 */
    {0x0c, 0x12, 0x12, 0x0e, 0x02, 0x0c, 0x00}, /* 9 */
    {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}, /* : */
    {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x08, 0x10}, /* ; */
    {0x00, 0x02, 0x04, 0x08, 0x04, 0x02, 0x00}, /* < */
    {0x00, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x00}, /* = */
    {0x00, 0x08, 0x04, 0x02, 0x04, 0x08, 0x00}, /* > */
    {0x04, 0x0a, 0x02, 0x04, 0x00, 0x04, 0x00}, /* ? */
    {0x0c, 0x12, 0x16, 0x16, 0x10, 0x0c, 0x00}, /* @ */
    {0x0c, 0x12, 0x12, 0x1e, 0x12, 0x12, 0x00}, /* A */
    {0x1c, 0x12, 0x1c, 0x12, 0x12, 0x1c, 0x00}, /* B */
    {0x0c, 0x12, 0x10, 0x10, 0x12, 0x0c, 0x00}, /* C */
    {0x1c, 0x12, 0x12, 0x12, 0x12, 0x1c, 0x00}, /* D */
    {0x1e, 0x10, 0x1c, 0x10, 0x10, 0x1e, 0x00}, /* E */
    {0x1e, 0x10, 0x1c, 0x10, 0x10, 0x10, 0x00}, /* F */
    {0x0c, 0x12, 0x10, 0x16, 0x12, 0x0e, 0x00}, /* G */
    {0x12, 0x12, 0x1e, 0x12, 0x12, 0x12, 0x00}, /* H */
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00}, /* I */
    {0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00}, /* J */
    {0x12, 0x14, 0x18, 0x18, 0x14, 0x12, 0x00}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x1e, 0x00}, /* L */
    {0x12, 0x1e, 0x1e, 0x12, 0x12, 0x12, 0x00}, /* M */
    {0x12, 0x1a, 0x1a, 0x16, 0x16, 0x12, 0x00}, /* N */
    {0x0c, 0x12, 0x12, 0x12, 0x12, 0x0c, 0x00}, /* O */
    {0x1c, 0x12, 0x12, 0x1c, 0x10, 0x10, 0x00}, /* P */
    {0x0c, 0x12, 0x12, 0x12, 0x1a, 0x0c, 0x02}, /* Q */
    {0x1c, 0x12, 0x12, 0x1c, 0x14, 0x12, 0x00}, /* R */
    {0x0c, 0x12, 0x08, 0x04, 0x12, 0x0c, 0x00}, /* S */
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* T */
    {0x12, 0x12, 0x12, 0x12, 0x12, 0x0c, 0x00}, /* U */
    {0x12, 0x12, 0x12, 0x12, 0x0c, 0x0c, 0x00}, /* V */
    {0x12, 0x12, 0x12, 0x1e, 0x1e, 0x12, 0x00}, /* W */
    {0x12, 0x12, 0x0c, 0x0c, 0x12, 0x12, 0x00}, /* X */
    {0x0a, 0x0a, 0x0a, 0x04, 0x04, 0x04, 0x00}, /* Y */
    {0x1e, 0x02, 0x04, 0x08, 0x10, 0x1e, 0x00}, /* Z */
    {0x0e, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00}, /* [ */
    {0x00, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00}, /* backslash */
    {0x0e, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00}, /* ] */
    {0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ^ */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00}, /* _ */
    {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ` */
    {0x00, 0x00, 0x0e, 0x12, 0x16, 0x0a, 0x00}, /* a */
    {0x10, 0x10, 0x1c, 0x12, 0x12, 0x1c, 0x00}, /* b */
    {0x00, 0x00, 0x0c, 0x10, 0x10, 0x0c, 0x00}, /* c */
    {0x02, 0x02, 0x0e, 0x12, 0x12, 0x0e, 0x00}, /* d */
    {0x00, 0x00, 0x0c, 0x16, 0x18, 0x0c, 0x00}, /* e */
    {0x04, 0x0a, 0x08, 0x1c, 0x08, 0x08, 0x00}, /* f */
    {0x00, 0x00, 0x0e, 0x12, 0x0c, 0x10, 0x0e}, /* g */
    {0x10, 0x10, 0x1c, 0x12, 0x12, 0x12, 0x00}, /* h */
    {0x04, 0x00, 0x0c, 0x04, 0x04, 0x0e, 0x00}, /* i */
    {0x02, 0x00, 0x02, 0x02, 0x02, 0x0a, 0x04}, /* j */
    {0x10, 0x10, 0x14, 0x18, 0x14, 0x12, 0x00}, /* k */
    {0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00}, /* l */
    {0x00, 0x00, 0x14, 0x1e, 0x12, 0x12, 0x00}, /* m */
    {0x00, 0x00, 0x1c, 0x12, 0x12, 0x12, 0x00}, /* n */
    {0x00, 0x00, 0x0c, 0x12, 0x12, 0x0c, 0x00}, /* o */
    {0x00, 0x00, 0x1c, 0x12, 0x12, 0x1c, 0x10}, /* p */
    {0x00, 0x00, 0x0e, 0x12, 0x12, 0x0e, 0x02}, /* q */
    {0x00, 0x00, 0x1c, 0x12, 0x10, 0x10, 0x00}, /* r */
    {0x00, 0x00, 0x0e, 0x18, 0x06, 0x1c, 0x00}, /* s */
    {0x08, 0x08, 0x1c, 0x08, 0x08, 0x06, 0x00}, /* t */
    {0x00, 0x00, 0x12, 0x12, 0x12, 0x0e, 0x00}, /* u */
    {0x00, 0x00, 0x0a, 0x0a, 0x0a, 0x04, 0x00}, /* v */
    {0x00, 0x00, 0x12, 0x12, 0x1e, 0x1e, 0x00}, /* w */
    {0x00, 0x00, 0x12, 0x0c, 0x0c, 0x12, 0x00}, /* x */
    {0x00, 0x00, 0x12, 0x12, 0x0a, 0x04, 0x08}, /* y */
    {0x00, 0x00, 0x1e, 0x04, 0x08, 0x1e, 0x00}, /* z */
    {0x02, 0x04, 0x0c, 0x04, 0x04, 0x02, 0x00}, /* { */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* | */
    {0x08, 0x04, 0x06, 0x04, 0x04, 0x08, 0x00}, /* } */
    {0x0a, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ~ */
};

static void glyph(uint8_t *fb, int x, int y, char c, uint8_t color) {
    if (c < ' ' || c > '~') {
        return;
    }
    const uint8_t *rows = font[c - ' '];
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row] & (0x10 >> col)) {
                plot(fb, x + col, y + row, color);
            }
        }
    }
}

/* Text with a black edge, so it reads over anything. Returns the next x. */
static int text(uint8_t *fb, int x, int y, const char *s, uint8_t color) {
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (const char *p = s; *p; p++, cx += 5) {
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
    return x + 5 * (int)strlen(s);
}

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

/* --- terminal: what you type, in green, fading away ------------------------ */

#define TERM_COLS 26
#define TERM_ROWS 10 /* down to just above the status text */
#define TERM_X 5
#define TERM_Y 4
#define TERM_PITCH 9 /* rows; characters are 5 apart */
#define TERM_LIT 205 /* a character once its first flash is over */

static char term_char[TERM_ROWS][TERM_COLS];
static uint8_t term_lum[TERM_ROWS][TERM_COLS];
static int term_row, term_col;
static bool term_prompt; /* this line starts with one */
static float term_blink, term_fade;

static void term_prompt_line(void) {
    term_prompt = true;
    term_char[term_row][0] = '>';
    term_lum[term_row][0] = 251;
    term_col = 2;
}

static void term_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0}, {70, 0, 70, 20}, {TERM_LIT, 40, 255, 90}, {251, 215, 255, 225}};
    gradient(palette, stops, 4);
    memset(term_lum, 0, sizeof(term_lum));
    term_row = 0;
    term_blink = term_fade = 0;
    term_prompt_line();
}

static void term_newline(bool prompt) {
    if (term_row < TERM_ROWS - 1) {
        term_row++;
    } else {
        memmove(term_char[0], term_char[1], sizeof(term_char) - sizeof(term_char[0]));
        memmove(term_lum[0], term_lum[1], sizeof(term_lum) - sizeof(term_lum[0]));
        memset(term_lum[TERM_ROWS - 1], 0, sizeof(term_lum[0]));
    }
    term_col = 0;
    term_prompt = false;
    if (prompt) {
        term_prompt_line();
    }
}

static void term_type(char c) {
    if (c == '\n') {
        term_newline(true);
    } else if (c == '\b') {
        if (term_col > (term_prompt ? 2 : 0)) {
            term_lum[term_row][--term_col] = 0;
        }
    } else if (c >= ' ' && c <= '~') {
        if (term_col >= TERM_COLS) {
            term_newline(false);
        }
        term_char[term_row][term_col] = c;
        term_lum[term_row][term_col] = 251;
        term_col++;
    }
}

static void term_frame(uint8_t *fb, const struct fx_input *in) {
    for (int i = 0; i < in->n_chars; i++) {
        term_type(in->chars[i]);
    }
    term_blink = in->n_chars ? 0 : term_blink + in->step;

    /* the flash goes in a moment; after that a character takes about 10 s */
    term_fade += in->step * 0.7f;
    int fade = (int)term_fade, flash = 4 + (int)(in->step * 5);
    term_fade -= fade;

    memset(fb, 0, PIXELS);
    bool any = false; /* anything lit, this line's prompt aside */
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            int lum = term_lum[r][c];
            if (lum == 0) {
                continue;
            }
            const bool prompt = term_prompt && r == term_row && c == 0;
            any = any || !prompt;
            glyph(fb, TERM_X + c * 5, TERM_Y + r * TERM_PITCH, term_char[r][c], (uint8_t)lum);
            lum -= lum > TERM_LIT ? flash : fade;
            /* this line's prompt stays */
            term_lum[r][c] = lum > (prompt ? 150 : 0) ? lum : prompt ? 150 : 0;
        }
    }
    /* everything has faded: start again at the top */
    if (!any && !in->n_chars && !(term_row == 0 && term_col == 2 && term_prompt)) {
        memset(term_lum, 0, sizeof(term_lum));
        term_row = 0;
        term_prompt_line();
    }

    /* the cursor: a block, steady while typing, blinking after */
    if ((int)(term_blink / 15) % 2 == 0 && term_col < TERM_COLS) {
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 4; x++) {
                plot(fb, TERM_X + term_col * 5 + x, TERM_Y + term_row * TERM_PITCH + y, TERM_LIT);
            }
        }
    }
}

/* --- code: a program being written in an alien script ---------------------- */

/*
 * Lines of made-up code, with indents, words and comments, written along the
 * bottom and pushed up and off the top. It writes slowly on its own; each
 * key adds two characters (the same key always gives the same one), so at
 * speed the page races. The letters are drawn up afresh from a few strokes
 * each time the firmware starts.
 */

#define CODE_COLS 27
#define CODE_ROWS 14
#define CODE_PITCH 7  /* rows; characters are 5 apart */
#define CODE_GLYPHS 64 /* 0 is the space */
#define CODE_LEVELS 63 /* palette: 4 colours x 63 brightness levels */
#define CODE_BOTTOM 86 /* top of the line being written */

static struct {
    uint8_t glyph, hue, heat;
} code_cell[CODE_ROWS][CODE_COLS];
static uint32_t code_glyphs[CODE_GLYPHS]; /* 4x5 dots */
static int code_col, code_indent, code_line_end, code_word, code_hue;
static bool code_comment;
static float code_pending, code_scroll;

static void code_make_glyphs(void) {
    for (int g = 1; g < CODE_GLYPHS; g++) {
        uint32_t bits = 0;
        int strokes = 2 + (int)(rnd() % 2);
        for (int s = 0; s < strokes; s++) {
            uint32_t r = rnd();
            int from = (r >> 4) % 3, to = from + 1 + (int)((r >> 8) % 3);
            if (r & 1) { /* across: on the top, middle or bottom row */
                int y = (int)((r >> 12) % 3) * 2;
                for (int x = from; x <= to && x < 4; x++) {
                    bits |= 1u << (y * 4 + x);
                }
            } else { /* down: on either side, or one in */
                static const uint8_t columns[] = {0, 3, 0, 3, 1, 2};
                int x = columns[(r >> 12) % 6];
                for (int y = from; y <= to + 1 && y < 5; y++) {
                    bits |= 1u << (y * 4 + x);
                }
            }
        }
        if (rnd() % 3 == 0) {
            bits |= 1u << (rnd() % 20); /* a dot */
        }
        code_glyphs[g] = bits;
    }
}

static void code_new_line(void) {
    memmove(code_cell[0], code_cell[1], sizeof(code_cell) - sizeof(code_cell[0]));
    memset(code_cell[CODE_ROWS - 1], 0, sizeof(code_cell[0]));
    code_scroll = CODE_PITCH;

    uint32_t r = rnd();
    code_indent += (int)(r % 3) - 1;
    code_indent = code_indent < 0 ? 0 : code_indent > 4 ? 4 : code_indent;
    code_col = code_indent * 2;
    code_line_end = code_col + 3 + (int)((r >> 4) % 17);
    if (code_line_end > CODE_COLS) {
        code_line_end = CODE_COLS;
    }
    code_comment = (r >> 12) % 7 == 0;
    code_word = 0;
    if ((r >> 16) % 9 == 0) {
        code_line_end = code_col; /* a blank line */
    }
}

static void code_write(uint8_t glyph) {
    if (code_col >= code_line_end) {
        code_new_line();
        return;
    }
    if (code_word == 0) {
        /* the next word, after a space: mostly green, some cyan and amber */
        uint32_t r = rnd();
        code_word = 1 + (int)(r % 6);
        code_hue = code_comment ? 3 : (r >> 8) % 10 < 6 ? 0 : (r >> 8) % 10 < 8 ? 1 : 2;
        if (code_col > code_indent * 2) {
            code_col++;
            return;
        }
    }
    code_word--;
    code_cell[CODE_ROWS - 1][code_col].glyph = glyph;
    code_cell[CODE_ROWS - 1][code_col].hue = (uint8_t)code_hue;
    code_cell[CODE_ROWS - 1][code_col].heat = CODE_LEVELS - 1;
    code_col++;
}

static void code_start(uint16_t *palette) {
    static const struct stop stops[] = {
        {0, 0, 0, 0},   {20, 0, 70, 25},   {50, 40, 255, 100},  {62, 220, 255, 225}, /* green */
        {63, 0, 0, 0},  {83, 0, 60, 80},   {113, 60, 230, 255}, {125, 225, 255, 255},
        {126, 0, 0, 0}, {146, 90, 50, 0},  {176, 255, 190, 40}, {188, 255, 250, 215},
        {189, 0, 0, 0}, {209, 40, 70, 60}, {239, 110, 170, 145}, {251, 200, 235, 220}, /* comments */
    };
    for (int h = 0; h < 4; h++) {
        gradient(palette, &stops[h * 4], 4);
    }
    if (code_glyphs[1] == 0) {
        code_make_glyphs();
    }
    memset(code_cell, 0, sizeof(code_cell));
    code_indent = 0;
    code_pending = code_scroll = 0;
    code_new_line();
    code_scroll = 0;
}

static void code_frame(uint8_t *fb, const struct fx_input *in) {
    for (int k = 0; k < in->n_keys; k++) {
        uint8_t glyph = 1 + in->keys[k] * 5 % (CODE_GLYPHS - 1);
        code_write(glyph);
        code_write(1 + (glyph + 30) % (CODE_GLYPHS - 1));
    }
    code_pending += in->step * (0.07f + in->energy * 0.5f);
    for (; code_pending >= 1; code_pending -= 1) {
        code_write(1 + rnd() % (CODE_GLYPHS - 1));
    }
    code_scroll -= in->step * 2;
    if (code_scroll < 0) {
        code_scroll = 0;
    }

    memset(fb, 0, PIXELS);
    int cool = 2 + (int)(in->step * 3);
    for (int r = 0; r < CODE_ROWS; r++) {
        int top = CODE_BOTTOM - (CODE_ROWS - 1 - r) * CODE_PITCH + (int)code_scroll;
        int base = 20 + 26 * r / (CODE_ROWS - 1); /* dimmer on the way up */
        for (int c = 0; c < CODE_COLS; c++) {
            if (code_cell[r][c].glyph == 0) {
                continue;
            }
            /* written white hot, cooling to its colour */
            int heat = code_cell[r][c].heat;
            code_cell[r][c].heat = heat > cool ? heat - cool : 0;
            uint8_t color = code_cell[r][c].hue * CODE_LEVELS + (heat > base ? heat : base);
            uint32_t bits = code_glyphs[code_cell[r][c].glyph];
            for (int y = 0; y < 5; y++) {
                for (int x = 0; x < 4; x++, bits >>= 1) {
                    if (bits & 1) {
                        plot(fb, 3 + c * 5 + x, top + 1 + y, color);
                    }
                }
            }
        }
    }
    /* the cursor */
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 4; x++) {
            plot(fb, 3 + (code_col < CODE_COLS ? code_col : CODE_COLS - 1) * 5 + x,
                 CODE_BOTTOM + (int)code_scroll + y, CODE_LEVELS - 1);
        }
    }
}

/* --- thermal: the keyboard through a heat camera ---------------------------- */

/*
 * The screen is the board seen from above: each key leaves a hot spot at its
 * own place, which spreads and cools from white through red, yellow and
 * green back into the cold blue. Two warm patches wander about so there is
 * something to see when idle, the whole picture warms up with typing speed,
 * and a sight follows the last key pressed.
 */

static float thermal_time, thermal_x, thermal_y, thermal_lock;
static int thermal_tx, thermal_ty;

static void thermal_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 35},      {45, 0, 25, 150},   {95, 0, 160, 130}, {140, 150, 225, 0},
                                        {175, 255, 210, 0}, {215, 255, 35, 0},  {251, 255, 245, 235}};
    gradient(palette, stops, 7);
    thermal_time = 0;
    thermal_x = thermal_tx = FX_W / 2;
    thermal_y = thermal_ty = FX_H / 2 - 12;
    thermal_lock = 0;
}

/* Warm the canvas around (cx, cy): `peak` in the middle, nothing at `radius`.
 * Added to what is there, or with `hold` only bringing it up to that. */
static void glow_spot(int cx, int cy, int radius, int peak, bool hold) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int far = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (far >= radius * radius || (unsigned)x >= FX_W || (unsigned)y >= FX_H) {
                continue;
            }
            int was = canvas[y * FX_W + x], v = peak * (radius * radius - far) / (radius * radius);
            v = hold ? (v > was ? v : was) : v + was;
            canvas[y * FX_W + x] = v > 251 ? 251 : v;
        }
    }
}

static void thermal_frame(uint8_t *fb, const struct fx_input *in) {
    thermal_time += in->step;

    /* the board fills the screen above the status text, the halves apart */
    for (int k = 0; k < in->n_keys; k++) {
        int col = key_col(in->keys[k]), row = key_row(in->keys[k]);
        thermal_tx = 13 + col * 12 + (col >= 5 ? 7 : 0);
        thermal_ty = 14 + row * 21;
        glow_spot(thermal_tx, thermal_ty, 9, 210, false);
        thermal_lock = 1;
    }
    /* the wanderers, warmer the faster you type */
    int warm = 60 + (int)(in->energy * 70);
    glow_spot(FX_W / 2 + (int)(sinf(thermal_time * 0.013f) * 52), 45 + (int)(sinf(thermal_time * 0.021f + 2) * 30),
                 15, warm, true);
    glow_spot(FX_W / 2 + (int)(sinf(thermal_time * 0.017f + 4) * 50), 50 + (int)(sinf(thermal_time * 0.011f) * 32),
                 12, warm, true);

    /* heat spreads to the pixels around and leaks away, the hottest fastest */
    for (int y = 1; y < FX_H - 1; y++) {
        uint8_t *row = &canvas[y * FX_W];
        for (int x = 1; x < FX_W - 1; x++) {
            int v = (row[x - 1] + row[x + 1] + row[x - FX_W] + row[x + FX_W] + 2) >> 2;
            row[x] = v > 1 ? v - (v >> 5) - ((x ^ y) & 1) : 0;
        }
    }

    /* the picture: the heat on a cold, noisy background that typing warms */
    int ambient = 12 + (int)(in->energy * 28);
    int band = (int)(thermal_time * 0.8f) % (FX_H + 40) - 20; /* a brighter line rolling down */
    for (int y = 0; y < FX_H; y++) {
        int lift = ambient + (y > band - 3 && y < band + 3 ? 7 : 0);
        uint32_t r = 0;
        for (int x = 0; x < FX_W; x++) {
            if ((x & 3) == 0) {
                r = rnd();
            }
            int v = canvas[y * FX_W + x] + lift + (int)(r & 7);
            r >>= 3;
            fb[y * FX_W + x] = v > 251 ? 251 : v;
        }
    }

    /* the sight: closes in on the key, then opens out again */
    float ease = clampf(0.25f * in->step, 0.0f, 1.0f);
    thermal_x += (thermal_tx - thermal_x) * ease;
    thermal_y += (thermal_ty - thermal_y) * ease;
    thermal_lock -= thermal_lock * clampf(0.04f * in->step, 0.0f, 1.0f);
    int size = 13 - (int)(thermal_lock * 6), arm = 4;
    int sx = (int)thermal_x, sy = (int)thermal_y;
    uint8_t color = thermal_lock > 0.5f ? FX_WHITE : FX_GREY;
    for (int corner = 0; corner < 4; corner++) {
        int dx = corner & 1 ? 1 : -1, dy = corner & 2 ? 1 : -1;
        line(fb, sx + dx * size, sy + dy * size, sx + dx * (size - arm), sy + dy * size, color);
        line(fb, sx + dx * size, sy + dy * size, sx + dx * size, sy + dy * (size - arm), color);
    }
    plot(fb, sx, sy, color);
}

/* --- tracker: a motion tracker; the faster you type, the closer they get --- */

/*
 * A half circle of range rings seen from its middle at the bottom, with a
 * pulse going out across it. Contacts only show, as a smudge that fades,
 * when the pulse passes over them. Idle, there are none; typing brings them
 * in, more of them and nearer with speed, and each key pulls one round to
 * its column. The figure at the bottom is the range of the nearest.
 *
 * This one draws the status itself: a gauge for each half's battery up the
 * sides, with its level under it, and the output at the top.
 */

#define TRACK_CX (FX_W / 2)
#define TRACK_CY 98
#define TRACK_FAR 94 /* the edge of what it sees, in pixels */
#define TRACK_CONTACTS 5
#define TRACK_DIM 45
#define TRACK_LINE 85
#define TRACK_TEXT 190

static struct {
    float angle, range; /* angle from the right, anticlockwise */
    bool on;
} contacts[TRACK_CONTACTS];
static float track_pulse, track_time;
static int track_next;

static void tracker_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 5, 14}, {TRACK_LINE, 0, 60, 95}, {TRACK_TEXT, 90, 190, 225}, {251, 235, 250, 255}};
    gradient(palette, stops, 4);
    memset(contacts, 0, sizeof(contacts));
    track_pulse = track_time = 0;
}

/* The top half of a circle. */
static void half_ring(uint8_t *buf, int r, uint8_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        plot(buf, TRACK_CX + x, TRACK_CY - y, c);
        plot(buf, TRACK_CX - x, TRACK_CY - y, c);
        plot(buf, TRACK_CX + y, TRACK_CY - x, c);
        plot(buf, TRACK_CX - y, TRACK_CY - x, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* A battery: a gauge from y 16 to 92 at `x`, its level under it. */
static void track_battery(uint8_t *fb, int x, int text_x, uint8_t percent, bool connected) {
    const int top = 16, bottom = 92;
    const bool low = !connected || (percent > 0 && percent <= 15);
    char level[3] = "--";
    int fill = 0;
    if (connected && percent > 0) {
        fill = (bottom - top - 1) * (percent > 100 ? 100 : percent) / 100;
        level[0] = '0' + (percent > 99 ? 99 : percent) / 10;
        level[1] = '0' + (percent > 99 ? 99 : percent) % 10;
    }
    for (int y = top; y <= bottom; y++) {
        for (int dx = -1; dx <= 5; dx++) {
            const bool edge = y == top || y == bottom || dx == 0 || dx == 4;
            const bool inside = dx > 0 && dx < 4 && y > bottom - 1 - fill;
            /* a gap every tenth of the way up */
            const bool gap = (bottom - y) % 8 == 0;
            plot(fb, x + dx, y, dx < 0 || dx > 4 ? 0 : edge ? TRACK_LINE : inside && !gap ? (low ? FX_RED : TRACK_TEXT) : 0);
        }
    }
    text(fb, text_x, 104, level, low ? FX_RED : TRACK_TEXT);
}

static void tracker_frame(uint8_t *fb, const struct fx_input *in) {
    const float energy = in->energy;
    track_time += in->step;

    /* each key brings a contact round to its column, left to right */
    for (int k = 0; k < in->n_keys; k++) {
        track_next = (track_next + 1) % TRACK_CONTACTS;
        contacts[track_next].angle = TWO_PI / 2 * (1.0f - (key_col(in->keys[k]) + 0.5f) / 10.0f);
        if (!contacts[track_next].on) {
            contacts[track_next].on = true;
            contacts[track_next].range = TRACK_FAR;
        }
    }

    /* the pulse, quicker with speed */
    const float pulse_was = track_pulse;
    track_pulse += in->step * (1.6f + energy * 3.0f);

    /* how many there are, and how near: both follow the typing speed */
    const int wanted = energy < 0.04f ? 0 : 1 + (int)(energy * 4.4f);
    float nearest = TRACK_FAR + 1;
    for (int i = 0; i < TRACK_CONTACTS; i++) {
        if (i < wanted && !contacts[i].on) {
            contacts[i].on = true;
            contacts[i].range = TRACK_FAR;
            contacts[i].angle = 0.3f + (rnd() & 255) / 255.0f * (TWO_PI / 2 - 0.6f);
        }
        if (!contacts[i].on) {
            continue;
        }
        if (i < wanted) {
            const float settle = 84 - energy * 64 + i * 6;
            contacts[i].range += (settle - contacts[i].range) * clampf(0.02f * in->step, 0.0f, 1.0f);
            contacts[i].angle += sinf(track_time * 0.02f + i * 2) * 0.004f * in->step;
        } else {
            /* backing off, then gone */
            contacts[i].range += 0.5f * in->step;
            if (contacts[i].range > TRACK_FAR) {
                contacts[i].on = false;
                continue;
            }
        }
        nearest = contacts[i].range < nearest ? contacts[i].range : nearest;
        if (pulse_was < contacts[i].range && track_pulse >= contacts[i].range) {
            glow_spot(TRACK_CX + (int)(cosf(contacts[i].angle) * contacts[i].range),
                      TRACK_CY - (int)(sinf(contacts[i].angle) * contacts[i].range), 6, 251, true);
        }
    }
    if (track_pulse > TRACK_FAR + 8) {
        track_pulse = 0;
    }

    /* the screen: rings every 20 pixels, spokes every 30 degrees */
    memset(fb, 0, PIXELS);
    for (int r = 20; r <= 80; r += 20) {
        half_ring(fb, r, r == 80 ? TRACK_LINE : TRACK_DIM);
    }
    for (int spoke = 1; spoke < 6; spoke++) {
        const float angle = spoke * (TWO_PI / 12);
        line(fb, TRACK_CX + (int)(cosf(angle) * 8), TRACK_CY - (int)(sinf(angle) * 8),
             TRACK_CX + (int)(cosf(angle) * TRACK_FAR), TRACK_CY - (int)(sinf(angle) * TRACK_FAR), TRACK_DIM);
    }
    line(fb, 12, TRACK_CY, FX_W - 13, TRACK_CY, TRACK_LINE);
    if (track_pulse < TRACK_FAR) {
        const int level = 230 - (int)(track_pulse * 1.3f);
        half_ring(fb, (int)track_pulse, (uint8_t)level);
        half_ring(fb, (int)track_pulse + 1, (uint8_t)(level * 2 / 3));
    }

    /* the contacts' smudges, over that, fading */
    const int fade = 3 + (int)(in->step * 2);
    for (int i = 0; i < PIXELS; i++) {
        const int v = canvas[i];
        if (v == 0) {
            continue;
        }
        if (v > fb[i]) {
            fb[i] = (uint8_t)v;
        }
        canvas[i] = v > fade ? v - fade : 0;
    }

    /* range of the nearest, twice the size of the other text */
    char range[3] = "--";
    if (nearest <= TRACK_FAR) {
        const int metres = (int)(nearest / 4);
        range[0] = '0' + metres / 10;
        range[1] = '0' + metres % 10;
    }
    for (int i = 0; i < 2; i++) {
        const uint8_t *rows = font[range[i] - ' '];
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 5; x++) {
                if (rows[y] & (0x10 >> x)) {
                    for (int d = 0; d < 4; d++) {
                        plot(fb, TRACK_CX - 12 + i * 10 + x * 2 + (d & 1), 102 + y * 2 + (d >> 1),
                             nearest < 30 ? FX_WHITE : TRACK_TEXT);
                    }
                }
            }
        }
    }
    glyph(fb, TRACK_CX + 10, 109, 'm', TRACK_TEXT);

    if (in->status) {
        track_battery(fb, 5, 10, in->status->battery[0], in->status->half_connected[0]);
        track_battery(fb, FX_W - 10, FX_W - 20, in->status->battery[1], in->status->half_connected[1]);
        const int len = (int)strlen(in->status->host);
        text(fb, FX_W - 12 - 5 * len, 6, in->status->host, in->status->host_connected ? TRACK_TEXT : FX_RED);
    }
}

/* --- the list --------------------------------------------------------------- */

static const struct {
    const char *name;
    void (*start)(uint16_t *palette);
    void (*frame)(uint8_t *fb, const struct fx_input *in);
    bool draws_status; /* shows the batteries and output its own way */
} effects[] = {
    {"WARP", warp_start, warp_frame, false},
    {"FIRE", fire_start, fire_frame, false},
    {"SPARKS", sparks_start, sparks_frame, false},
    {"TERMINAL", term_start, term_frame, false},
    {"CODE", code_start, code_frame, false},
    {"THERMAL", thermal_start, thermal_frame, false},
    {"TRACKER", tracker_start, tracker_frame, true},
};

#define FX_COUNT ((int)(sizeof(effects) / sizeof(effects[0])))

int fx_count(void) { return FX_COUNT; }

const char *fx_name(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].name : ""; }

bool fx_draws_status(int fx) { return fx >= 0 && fx < FX_COUNT && effects[fx].draws_status; }

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

/* A battery level, "--" when its half isn't connected or hasn't said. */
static int battery_text(uint8_t *fb, int x, int y, uint8_t percent, bool connected) {
    char buf[3] = "--";
    if (connected && percent > 0) {
        if (percent > 99) {
            percent = 99; /* two digits: "99" reads as full */
        }
        buf[0] = '0' + percent / 10;
        buf[1] = '0' + percent % 10;
    }
    return text(fb, x, y, buf, !connected || (percent > 0 && percent <= 15) ? FX_RED : FX_WHITE);
}

void fx_draw_status(uint8_t *fb, const struct fx_status *status, const char *banner) {
    if (status) {
        /* kept clear of the panel's rounded corners */
        const int y = FX_H - 16;
        /* left/right, as the Field screen has them */
        int x = battery_text(fb, 10, y, status->battery[0], status->half_connected[0]);
        x = text(fb, x, y, "/", FX_WHITE);
        battery_text(fb, x, y, status->battery[1], status->half_connected[1]);

        int len = (int)strlen(status->host);
        text(fb, FX_W - 10 - 5 * len + 1, y, status->host, status->host_connected ? FX_WHITE : FX_RED);
    }

    if (banner) {
        text(fb, 10, 9, banner, FX_WHITE);
    }
}
