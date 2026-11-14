/*
 * Animations for the Prospector dongle's screen: see fx.h.
 *
 * Each animation is a start function (palette, state) and a frame function.
 * `canvas` is one spare picture they share for whatever has to survive from
 * frame to frame (flame heat, fading trails); the frame handed in is redrawn
 * completely every time, because the banner is written over it. The
 * caller owns the canvas's memory (fx_set_canvas): the dongle has none to
 * spare for it.
 */

#include "fx.h"

#include <math.h>
#include <stdio.h>
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
 * its column. The figure top right is the range of the nearest.
 *
 * It adds a gauge for each half's battery up the sides, over the level that
 * every animation has in its bottom corner.
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

/* A battery: a gauge from y 16 to 90 at `x`. */
static void track_battery(uint8_t *fb, int x, uint8_t percent, bool connected) {
    const int top = 16, bottom = 90;
    const bool low = !connected || (percent > 0 && percent <= 15);
    int fill = 0;
    if (connected && percent > 0) {
        fill = (bottom - top - 1) * (percent > 100 ? 100 : percent) / 100;
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
                        plot(fb, FX_W - 40 + i * 10 + x * 2 + (d & 1), 6 + y * 2 + (d >> 1),
                             nearest < 30 ? FX_WHITE : TRACK_TEXT);
                    }
                }
            }
        }
    }
    glyph(fb, FX_W - 18, 13, 'm', TRACK_TEXT);

    if (in->status) {
        track_battery(fb, 5, in->status->battery[0], in->status->half_connected[0]);
        track_battery(fb, FX_W - 10, in->status->battery[1], in->status->half_connected[1]);
    }
}

/* --- life: Conway's Game of Life, fed by the keys --------------------------- */

/*
 * 70 by 60 cells, joined edge to edge. Each key drops a clump of cells at
 * its own place on the board; the generations tick over slowly when idle
 * and quickly at speed. A cell is born white and ages through yellow and
 * green to blue, and leaves a glow behind when it dies. When the board dies
 * out or settles into a pattern that only repeats, a new clump is dropped
 * in somewhere.
 */

#define LIFE_W (FX_W / 2)
#define LIFE_H (FX_H / 2)
#define LIFE_CELLS (LIFE_W * LIFE_H)
#define LIFE_TRAIL 50 /* palette: 1..50 the glow, 60..251 a live cell by age */
#define LIFE_OLD 60

static float life_tick;
static int life_count[2], life_settled; /* cells alive one and two generations ago */

static void life_start(uint16_t *palette) {
    static const struct stop stops[] = {{0, 0, 0, 0},         {LIFE_TRAIL, 70, 0, 110}, {LIFE_OLD, 0, 60, 170},
                                        {130, 0, 190, 110},   {200, 210, 235, 0},       {251, 255, 255, 240}};
    gradient(palette, stops, 6);
    life_tick = 0;
    life_count[0] = life_count[1] = life_settled = 0;
}

/* A clump: about half the cells in a 5x5 square. */
static void life_seed(int cx, int cy) {
    uint32_t r = rnd() | rnd() << 16;
    for (int i = 0; i < 25; i++, r >>= 1) {
        if (r & 1) {
            int x = (cx + i % 5 - 2 + LIFE_W) % LIFE_W, y = (cy + i / 5 - 2 + LIFE_H) % LIFE_H;
            canvas[y * LIFE_W + x] = 251;
        }
    }
}

/* The canvas holds three boards: the cells (0 dead, else its colour), the
 * next generation while it is worked out, and the glow. */
static void life_step(void) {
    uint8_t *now = canvas, *next = canvas + LIFE_CELLS, *glow = canvas + 2 * LIFE_CELLS;
    int alive = 0;
    for (int y = 0; y < LIFE_H; y++) {
        const uint8_t *up = &now[(y + LIFE_H - 1) % LIFE_H * LIFE_W], *row = &now[y * LIFE_W],
                      *down = &now[(y + 1) % LIFE_H * LIFE_W];
        for (int x = 0; x < LIFE_W; x++) {
            const int l = x ? x - 1 : LIFE_W - 1, r = x + 1 < LIFE_W ? x + 1 : 0;
            const int around = !!up[l] + !!up[x] + !!up[r] + !!row[l] + !!row[r] + !!down[l] + !!down[x] + !!down[r];
            const int age = row[x];
            int v = 0;
            if (age) {
                if (around == 2 || around == 3) {
                    v = age > LIFE_OLD + 6 ? age - 6 : LIFE_OLD;
                } else {
                    glow[y * LIFE_W + x] = LIFE_TRAIL;
                }
            } else if (around == 3) {
                v = 251;
            }
            next[y * LIFE_W + x] = (uint8_t)v;
            alive += v != 0;
        }
    }
    memcpy(now, next, LIFE_CELLS);

    /* still lifes and blinkers keep the same count from one generation to the
     * next but one */
    life_settled = alive == life_count[1] ? life_settled + 1 : 0;
    life_count[1] = life_count[0];
    life_count[0] = alive;
    if (alive < 25 || life_settled > 14) {
        life_seed((int)(rnd() % LIFE_W), (int)(rnd() % (LIFE_H - 14)));
        life_settled = 0;
    }
}

static void life_frame(uint8_t *fb, const struct fx_input *in) {
    for (int k = 0; k < in->n_keys; k++) {
        const int col = key_col(in->keys[k]), row = key_row(in->keys[k]);
        life_seed((13 + col * 12 + (col >= 5 ? 7 : 0)) / 2, (14 + row * 21) / 2);
    }
    /* 3 generations a second idle, 15 flat out */
    life_tick += in->step * (0.1f + in->energy * 0.4f);
    for (int n = 0; life_tick >= 1 && n < 2; n++) {
        life_tick -= 1;
        life_step();
    }

    const uint8_t *now = canvas;
    uint8_t *glow = canvas + 2 * LIFE_CELLS;
    const int fade = 1 + (int)in->step;
    for (int y = 0; y < LIFE_H; y++) {
        uint8_t *out = &fb[y * 2 * FX_W];
        for (int x = 0; x < LIFE_W; x++) {
            const int i = y * LIFE_W + x;
            const uint8_t c = now[i] ? now[i] : glow[i];
            glow[i] = glow[i] > fade ? glow[i] - fade : 0;
            out[x * 2] = out[x * 2 + 1] = c;
        }
        memcpy(out + FX_W, out, FX_W);
    }
}

/* --- hop: a little creature running through a platform game ---------------- */

/*
 * It runs by itself, slowly when idle and fast at speed, and jumps the
 * ponds, rocks, logs and bushes in its way, picking up the stars over them
 * (counted top right). Each key plants a flower under its column and, when
 * nothing is coming, gets a little hop out of it. Clouds, two rows of hills
 * and the ground scroll at different speeds.
 *
 * Places in the world are whole pixels, 24 bits that wrap; hop_at() says
 * where one is on the screen.
 */

enum {
    HOP_NONE, /* see-through, in a sprite */
    HOP_SKY,  /* 6 shades, top to horizon */
    HOP_SUN = HOP_SKY + 6,
    HOP_SUN_RIM,
    HOP_CLOUD,
    HOP_CLOUD_SHADE,
    HOP_HILL_FAR,
    HOP_HILL_NEAR,
    HOP_GRASS_TOP,
    HOP_GRASS,
    HOP_DIRT,
    HOP_DIRT_DARK,
    HOP_WATER,
    HOP_WATER_LIGHT,
    HOP_ROCK,
    HOP_ROCK_LIGHT,
    HOP_ROCK_DARK,
    HOP_WOOD,
    HOP_WOOD_LIGHT,
    HOP_LEAF,
    HOP_LEAF_DARK,
    HOP_BERRY,
    HOP_OUTLINE,
    HOP_BODY,
    HOP_BELLY,
    HOP_CHEEK,
    HOP_FEET,
    HOP_STAR,
    HOP_STAR_LIGHT,
    HOP_FLOWER, /* 4 colours */
};

#define HOP_GROUND 92 /* the top of the grass */
#define HOP_X 30      /* the creature's left edge */
#define HOP_WIDE 12
#define HOP_COLUMNS 32 /* of 8 pixels: the screen is 18 */
#define HOP_FLOWERS 12

enum { HOP_FLAT, HOP_POND, HOP_ROCKS, HOP_LOG, HOP_BUSH };

static struct {
    uint8_t kind, star;
} hop_column[HOP_COLUMNS];
static struct {
    uint32_t at;
    uint8_t color, grown;
} hop_flower[HOP_FLOWERS];
static uint32_t hop_pos;   /* the left of the screen, in 256ths of a pixel */
static uint32_t hop_made;  /* the next column to make up */
static int hop_clear, hop_more; /* columns before the next thing; pond still to come */
static bool hop_jumping;
static uint32_t hop_jump_from;
static int hop_jump_far, hop_jump_high;
static int hop_stars, hop_next_flower;
static float hop_sparkle, hop_time;

/* Where a place in the world is across the screen (can be off either side). */
static inline int hop_at(uint32_t place) { return (int32_t)((place - (hop_pos >> 8)) << 8) >> 8; }

static void hop_start(uint16_t *palette) {
    static const uint8_t colors[][3] = {
        [HOP_SKY] = {70, 150, 255},       {95, 170, 255},  {120, 190, 255}, {150, 210, 255}, {180, 225, 255}, {210, 240, 255},
        [HOP_SUN] = {255, 240, 120},      [HOP_SUN_RIM] = {255, 215, 70},
        [HOP_CLOUD] = {255, 255, 255},    [HOP_CLOUD_SHADE] = {215, 232, 250},
        [HOP_HILL_FAR] = {130, 205, 170}, [HOP_HILL_NEAR] = {90, 190, 110},
        [HOP_GRASS_TOP] = {150, 235, 90}, [HOP_GRASS] = {70, 190, 70},
        [HOP_DIRT] = {190, 125, 70},      [HOP_DIRT_DARK] = {150, 90, 50},
        [HOP_WATER] = {40, 120, 230},     [HOP_WATER_LIGHT] = {150, 215, 255},
        [HOP_ROCK] = {150, 150, 165},     [HOP_ROCK_LIGHT] = {200, 200, 215}, [HOP_ROCK_DARK] = {105, 105, 125},
        [HOP_WOOD] = {150, 95, 45},       [HOP_WOOD_LIGHT] = {225, 180, 115},
        [HOP_LEAF] = {40, 160, 60},       [HOP_LEAF_DARK] = {20, 110, 45},    [HOP_BERRY] = {235, 50, 70},
        [HOP_OUTLINE] = {70, 35, 30},     [HOP_BODY] = {255, 150, 50},        [HOP_BELLY] = {255, 235, 190},
        [HOP_CHEEK] = {255, 120, 150},    [HOP_FEET] = {210, 90, 30},
        [HOP_STAR] = {255, 215, 40},      [HOP_STAR_LIGHT] = {255, 250, 190},
        [HOP_FLOWER] = {255, 90, 120},    {255, 255, 255}, {190, 120, 255}, {255, 170, 40},
    };
    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        palette[i] = rgb(colors[i][0], colors[i][1], colors[i][2]);
    }
    memset(hop_column, 0, sizeof(hop_column));
    memset(hop_flower, 0, sizeof(hop_flower));
    hop_pos = hop_made = 0;
    hop_clear = 20;
    hop_more = 0;
    hop_jumping = false;
    hop_stars = 0;
    hop_sparkle = hop_time = 0;
}

/* A sprite drawn as text, a letter a pixel; `tint` is the colour for 'c'. */
static void hop_sprite(uint8_t *fb, int x, int y, const char *const *rows, int n, uint8_t tint) {
    for (int row = 0; row < n; row++) {
        for (int col = 0; rows[row][col]; col++) {
            uint8_t c;
            switch (rows[row][col]) {
            case 'k': c = HOP_OUTLINE; break;
            case 'o': c = HOP_BODY; break;
            case 'b': c = HOP_BELLY; break;
            case 'p': c = HOP_CHEEK; break;
            case 'f': c = HOP_FEET; break;
            case 'W': c = FX_WHITE; break;
            case 'K': c = FX_BLACK; break;
            case 'w': c = HOP_CLOUD; break;
            case 's': c = HOP_CLOUD_SHADE; break;
            case 'g': c = HOP_ROCK; break;
            case 'G': c = HOP_ROCK_LIGHT; break;
            case 'd': c = HOP_ROCK_DARK; break;
            case 'n': c = HOP_WOOD; break;
            case 'N': c = HOP_WOOD_LIGHT; break;
            case 'l': c = HOP_LEAF; break;
            case 'L': c = HOP_LEAF_DARK; break;
            case 'r': c = HOP_BERRY; break;
            case 'y': c = HOP_STAR; break;
            case 'Y': c = HOP_STAR_LIGHT; break;
            case 'c': c = tint; break;
            default: continue;
            }
            plot(fb, x + col, y + row, c);
        }
    }
}

static const char *const hop_body[] = {
    "..kk....kk..",
    ".kook..kook.",
    ".kooookoook.",
    "kooooooooook",
    "koooWKooWKok",
    "koooWKooWKok",
    "kopoooooopok",
    "koobbbbbbook",
    ".kobbbbbbok.",
    "..kkkkkkkk..",
};
static const char *const hop_blink[] = {"kooooooooook", "kookkkookkok"};
static const char *const hop_feet[][2] = {
    {"..ff....ff..", "............"}, /* standing */
    {".ff......ff.", "............"}, /* striding */
    {"...ff..ff...", "............"}, /* tucked up */
};
static const char *const hop_cloud[] = {
    "....www...........", "..wwwwwww..www....", ".wwwwwwwwwwwwwww..",
    "wwwwwwwwwwwwwwwwww", "swwwwwwwwwwwwwwwws", ".sssssssssssssss..",
};
static const char *const hop_rock[] = {"..gGGg..", ".gGGggg.", "gGGggggd", "gGgggggd", "gggggddd", "ggggdddd", "gdgddddd"};
static const char *const hop_log[] = {".nnnnnn.", "nNNnnnnn", "nNnNnnnn", "nNNnnnnn", "nnnnnnnn", ".nnnnnn."};
static const char *const hop_bush[] = {"..l..l..", ".lll.ll.", "lllrlllL", "llllllLL", "lrllLlrL", "lllLLLLL", ".lLLLLL."};
static const char *const hop_star[] = {"...y...", "..yYy..", "yyyYyyy", ".yYYYy.", "..yYy..", ".yy.yy.", "y.....y"};
static const char *const hop_bloom[] = {".c.", "cyc", ".c."};

static void hop_make_column(float energy) {
    uint8_t kind = HOP_FLAT, star = 0;
    if (hop_more > 0) {
        kind = HOP_POND;
        hop_more--;
    } else if (hop_clear > 0) {
        hop_clear--;
    } else {
        const uint32_t r = rnd();
        kind = r % 10 < 3 ? HOP_POND : HOP_ROCKS + (r >> 4) % 3;
        /* wider ponds once it is moving */
        hop_more = kind == HOP_POND && energy > 0.35f && (r & 0x100) ? 1 : 0;
        star = (r >> 10) % 3 != 0;
        hop_clear = 5 + (int)((r >> 12) % 7);
    }
    hop_column[hop_made % HOP_COLUMNS].kind = kind;
    hop_column[hop_made % HOP_COLUMNS].star = star;
    hop_made++;
}

static void hop_frame(uint8_t *fb, const struct fx_input *in) {
    hop_time += in->step;
    hop_pos += (uint32_t)((0.7f + in->energy * 3.0f) * in->step * 256);
    const uint32_t left = hop_pos >> 8, me = left + HOP_X;

    while (hop_at(hop_made * 8) < FX_W + 24) {
        hop_make_column(in->energy);
    }

    /* the jump: its shape is fixed by where it must land, so a change of
     * speed in the air can't drop it short */
    int lift = 0;
    if (hop_jumping) {
        const int gone = hop_at(me) - hop_at(hop_jump_from);
        if (gone >= hop_jump_far) {
            hop_jumping = false;
        } else {
            lift = 4 * hop_jump_high * gone * (hop_jump_far - gone) / (hop_jump_far * hop_jump_far);
        }
    }
    if (!hop_jumping) {
        /* the first thing in the way within 5 columns of its nose */
        const uint32_t nose = me + HOP_WIDE;
        int ahead = -1;
        for (int i = 0; i < 6 && ahead < 0; i++) {
            if (hop_column[(nose / 8 + i) % HOP_COLUMNS].kind != HOP_FLAT) {
                ahead = i;
            }
        }
        if (ahead >= 0) {
            uint32_t starts = (nose / 8 + ahead) * 8, ends = starts + 8;
            while (hop_column[(ends / 8) % HOP_COLUMNS].kind != HOP_FLAT) {
                ends += 8;
            }
            if (hop_at(starts) - hop_at(nose) <= 7) {
                hop_jumping = true;
                hop_jump_from = me;
                hop_jump_far = hop_at(ends) + 5 - hop_at(me);
                hop_jump_high = hop_jump_far * 6 / 10;
                hop_jump_high = hop_jump_high < 16 ? 16 : hop_jump_high > 27 ? 27 : hop_jump_high;
            }
        } else if (in->n_keys) {
            hop_jumping = true; /* a hop for the key */
            hop_jump_from = me;
            hop_jump_far = 16;
            hop_jump_high = 7;
        }
    }

    /* each key plants a flower under its column, if there is ground there */
    for (int k = 0; k < in->n_keys; k++) {
        const uint32_t at = left + key_x(in->keys[k]);
        if (hop_column[(at / 8) % HOP_COLUMNS].kind == HOP_FLAT) {
            hop_next_flower = (hop_next_flower + 1) % HOP_FLOWERS;
            hop_flower[hop_next_flower].at = at;
            hop_flower[hop_next_flower].color = HOP_FLOWER + rnd() % 4;
            hop_flower[hop_next_flower].grown = 1;
        }
    }

    /* sky, sun, clouds */
    for (int y = 0; y < HOP_GROUND; y++) {
        memset(&fb[y * FX_W], HOP_SKY + y * 6 / HOP_GROUND, FX_W);
    }
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            if (x * x + y * y <= 64) {
                plot(fb, 112 + x, 22 + y, x * x + y * y <= 42 ? HOP_SUN : HOP_SUN_RIM);
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        static const uint8_t cloud_x[] = {20, 95, 160}, cloud_y[] = {10, 30, 17};
        hop_sprite(fb, (int)((cloud_x[i] - left / 6) % 200) - 30, cloud_y[i], hop_cloud, 6, 0);
    }

    /* hills, far and near */
    for (int x = 0; x < FX_W; x++) {
        const float far = (x + (left / 4) % 4096) * 0.0491f, near = (x + (left / 2) % 4096) * 0.0736f;
        const int far_top = HOP_GROUND - 20 - (int)(sinf(far) * 9 + sinf(far * 3 + 1) * 4);
        const int near_top = HOP_GROUND - 9 - (int)(sinf(near + 2) * 6 + sinf(near * 2) * 3);
        for (int y = far_top; y < HOP_GROUND; y++) {
            fb[y * FX_W + x] = y >= near_top ? HOP_HILL_NEAR : HOP_HILL_FAR;
        }
    }

    /* the ground and the ponds */
    for (int x = 0; x < FX_W; x++) {
        const uint32_t place = left + x;
        const bool pond = hop_column[(place / 8) % HOP_COLUMNS].kind == HOP_POND;
        for (int y = HOP_GROUND; y < FX_H; y++) {
            uint8_t c;
            if (pond) {
                const int surface = HOP_GROUND + 4 + (((place + (uint32_t)(hop_time / 6)) & 7) < 3);
                c = y < HOP_GROUND + 4 ? HOP_SKY + 5 : y <= surface ? HOP_WATER_LIGHT : HOP_WATER;
            } else {
                c = y == HOP_GROUND ? HOP_GRASS_TOP
                    : y < HOP_GROUND + 4 + ((place & 3) == 1) ? HOP_GRASS
                    : ((place * 7 + y * 13) % 23) < 3 ? HOP_DIRT_DARK : HOP_DIRT;
            }
            fb[y * FX_W + x] = c;
        }
    }

    for (int i = 0; i < HOP_FLOWERS; i++) {
        const int x = hop_at(hop_flower[i].at), grown = hop_flower[i].grown;
        if (grown == 0 || x < -4 || x > FX_W + 4) {
            hop_flower[i].grown = x < -4 ? 0 : grown;
            continue;
        }
        for (int stem = 1; stem <= grown / 2; stem++) {
            plot(fb, x, HOP_GROUND - stem, HOP_LEAF);
        }
        if (grown >= 6) {
            hop_sprite(fb, x - 1, HOP_GROUND - grown / 2 - 3, hop_bloom, 3, hop_flower[i].color);
        }
        hop_flower[i].grown = grown < 10 ? grown + 1 : grown;
    }

    /* what is in the way, and the stars over it */
    const int my_top = HOP_GROUND - 12 - lift;
    for (uint32_t col = left / 8; hop_at(col * 8) < FX_W; col++) {
        const int x = hop_at(col * 8);
        const uint8_t kind = hop_column[col % HOP_COLUMNS].kind;
        if (kind == HOP_ROCKS) {
            hop_sprite(fb, x, HOP_GROUND - 6, hop_rock, 7, 0);
        } else if (kind == HOP_LOG) {
            hop_sprite(fb, x, HOP_GROUND - 5, hop_log, 6, 0);
        } else if (kind == HOP_BUSH) {
            hop_sprite(fb, x, HOP_GROUND - 6, hop_bush, 7, 0);
        }
        if (hop_column[col % HOP_COLUMNS].star) {
            const int star_y = HOP_GROUND - 30 + (int)(sinf(hop_time * 0.15f + col) * 1.5f);
            if (x < HOP_X + HOP_WIDE && x + 7 > HOP_X && star_y + 7 > my_top - 4) {
                hop_column[col % HOP_COLUMNS].star = 0;
                hop_stars = (hop_stars + 1) % 1000;
                hop_sparkle = 8;
            } else {
                hop_sprite(fb, x, star_y, hop_star, 7, 0);
            }
        }
    }

    /* the creature: feet stride with the ground, eyes blink now and then */
    hop_sprite(fb, HOP_X, my_top, hop_body, 10, 0);
    if ((int)(hop_time / 4) % 40 == 0) {
        hop_sprite(fb, HOP_X, my_top + 4, hop_blink, 2, 0);
    }
    hop_sprite(fb, HOP_X, my_top + 10, hop_feet[hop_jumping ? 2 : (left / 5) & 1], 2, 0);
    if (hop_sparkle > 0) {
        const int reach = 10 - (int)hop_sparkle;
        for (int i = 0; i < 4; i++) {
            plot(fb, HOP_X + 6 + (i & 1 ? reach : -reach), my_top - 2 + (i & 2 ? reach : -reach) / 2, HOP_STAR_LIGHT);
            plot(fb, HOP_X + 6 + (i & 1 ? reach : -reach) / 2, my_top - 2 + (i & 2 ? reach : -reach), HOP_STAR);
        }
        hop_sparkle -= in->step;
    }

    /* stars picked up */
    char count[4] = {'0' + hop_stars / 100, '0' + hop_stars / 10 % 10, '0' + hop_stars % 10, '\0'};
    const char *shown = hop_stars >= 100 ? count : hop_stars >= 10 ? count + 1 : count + 2;
    const int width = 5 * (int)strlen(shown);
    hop_sprite(fb, FX_W - 12 - width - 9, 6, hop_star, 7, 0);
    text(fb, FX_W - 12 - width, 6, shown, FX_WHITE);
}

/* --- debug: everything the dongle knows, as text ---------------------------- */

/*
 * Not an animation: a page of readings for finding faults with connections,
 * switches and the firmware (README, "Debug screen"). Top to bottom: the
 * dongle itself; a table of the three Bluetooth links; the profiles, USB
 * and the output in use; layer, typing speed and modifiers; then the last
 * few raw events beside a map of the keys, and the worst key bounce seen.
 */

enum { DBG_BG, DBG_DIM, DBG_LABEL, DBG_TEXT, DBG_BRIGHT, DBG_GOOD, DBG_WARN, DBG_HEAT /* ..DBG_HEAT + 15 */ };

static uint8_t debug_heat[40]; /* how lately each key was pressed */
static float debug_fade;

static void debug_start(uint16_t *palette) {
    palette[DBG_BG] = rgb(0, 5, 14);
    palette[DBG_DIM] = rgb(0, 45, 70);
    palette[DBG_LABEL] = rgb(50, 120, 150);
    palette[DBG_TEXT] = rgb(150, 220, 245);
    palette[DBG_BRIGHT] = rgb(255, 255, 255);
    palette[DBG_GOOD] = rgb(70, 235, 130);
    palette[DBG_WARN] = rgb(255, 190, 40);
    for (int i = 0; i < 16; i++) {
        palette[DBG_HEAT + i] = rgb(i * 6, 45 + i * 12, 70 + i * 11);
    }
    memset(debug_heat, 0, sizeof(debug_heat));
}

/* Text with nothing round it. Returns the next x. */
static int put(uint8_t *fb, int x, int y, const char *s, uint8_t color) {
    for (; *s; s++, x += 5) {
        glyph(fb, x, y, *s, color);
    }
    return x;
}

/* The same, ending at `right`. */
static void put_right(uint8_t *fb, int right, int y, const char *s, uint8_t color) {
    put(fb, right - 5 * (int)strlen(s), y, s, color);
}

static void rule(uint8_t *fb, int y) { line(fb, 4, y, FX_W - 5, y, DBG_DIM); }

/* Signal strength as four bars, 7 wide and 7 high. */
static void signal_bars(uint8_t *fb, int x, int y, const struct fx_link *link) {
    const int rssi = link->rssi;
    const int lit = !link->up || !link->rssi_known ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
    const uint8_t color = lit >= 3 ? DBG_GOOD : lit == 2 ? DBG_WARN : FX_RED;
    for (int bar = 0; bar < 4; bar++) {
        const int high = 1 + bar * 2;
        for (int dy = 0; dy < high; dy++) {
            plot(fb, x + bar * 2, y + 6 - dy, bar < lit ? color : DBG_DIM);
        }
    }
}

/* Column right edges of the links table. */
#define DBG_COL_LEVEL 39
#define DBG_COL_RSSI 59
#define DBG_COL_INTERVAL 81
#define DBG_COL_LATENCY 96
#define DBG_COL_DROPS 111
#define DBG_COL_REASON 124
#define DBG_COL_AGE 138

/* One row of the table: `level` is the battery, or the output for the computer. */
static void debug_link(uint8_t *fb, int y, char name, const struct fx_link *link, const char *level,
                       uint8_t level_color, int age_s) {
    char buf[12];
    const char label[2] = {name, '\0'};
    put(fb, 5, y, label, link->up ? DBG_BRIGHT : FX_RED);
    signal_bars(fb, 12, y, link);
    put_right(fb, DBG_COL_LEVEL, y, level, level_color);
    if (link->up) {
        if (link->rssi_known) {
            snprintf(buf, sizeof(buf), "%d", link->rssi);
            put_right(fb, DBG_COL_RSSI, y, buf,
                      link->rssi > -65 ? DBG_GOOD : link->rssi > -80 ? DBG_WARN : FX_RED);
        }
        /* 1.25 ms units, to a tenth of a millisecond when that isn't .0 */
        const int tenths = link->interval * 25 / 2;
        if (tenths % 10 == 0 || tenths >= 1000) {
            snprintf(buf, sizeof(buf), "%d", tenths / 10);
        } else {
            snprintf(buf, sizeof(buf), "%d.%d", tenths / 10, tenths % 10);
        }
        put_right(fb, DBG_COL_INTERVAL, y, buf, DBG_TEXT);
        snprintf(buf, sizeof(buf), "%d", link->latency > 99 ? 99 : link->latency);
        put_right(fb, DBG_COL_LATENCY, y, buf, DBG_TEXT);
    } else {
        put_right(fb, DBG_COL_RSSI, y, "--", FX_RED);
    }
    snprintf(buf, sizeof(buf), "%d", link->drops > 99 ? 99 : link->drops);
    put_right(fb, DBG_COL_DROPS, y, buf, link->drops ? DBG_WARN : DBG_LABEL);
    if (link->reason) {
        snprintf(buf, sizeof(buf), "%02X", link->reason);
        put_right(fb, DBG_COL_REASON, y, buf, DBG_WARN);
    }
    if (age_s >= 0) {
        snprintf(buf, sizeof(buf), "%d", age_s > 999 ? 999 : age_s);
        put_right(fb, DBG_COL_AGE, y, buf, age_s > 600 ? DBG_WARN : DBG_TEXT);
    }
}

static const char *reset_name(uint32_t cause) {
    /* Zephyr's RESET_* bits, the likeliest fault first */
    static const struct {
        uint16_t bit;
        const char *name;
    } names[] = {{1 << 4, "WDOG"}, {1 << 8, "LOCK"}, {1 << 2, "BRWN"}, {1 << 1, "SOFT"},
                 {1 << 0, "PIN"},  {1 << 7, "WAKE"}, {1 << 5, "DBG"},  {1 << 3, "POR"}};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (cause & names[i].bit) {
            return names[i].name;
        }
    }
    return cause ? "?" : "POR";
}

static void debug_frame(uint8_t *fb, const struct fx_input *in) {
    static const struct fx_debug none;
    const struct fx_debug *d = in->debug ? in->debug : &none;
    char buf[32];

    memset(fb, DBG_BG, PIXELS);

    /* the dongle: up time, activity, this screen's speed; why it last
     * restarted and how full LVGL's memory is */
    int x = put(fb, 12, 4, "DBG", DBG_BRIGHT) + 5;
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)(d->uptime_s / 3600), (unsigned)(d->uptime_s / 60 % 60),
             (unsigned)(d->uptime_s % 60));
    x = put(fb, x, 4, buf, DBG_TEXT) + 5;
    x = put(fb, x, 4, d->activity == 0 ? "ACT" : d->activity == 1 ? "IDL" : "SLP", d->activity ? DBG_WARN : DBG_GOOD) + 5;
    snprintf(buf, sizeof(buf), "%uF %uMS", d->fps, d->frame_ms);
    put(fb, x, 4, buf, DBG_TEXT);

    x = put(fb, 7, 12, "RST", DBG_LABEL) + 3;
    const bool fault = (d->reset_cause & (1 << 4 | 1 << 8 | 1 << 2)) != 0;
    x = put(fb, x, 12, reset_name(d->reset_cause), fault ? FX_RED : DBG_TEXT) + 3;
    snprintf(buf, sizeof(buf), "%02X", (unsigned)(d->reset_cause & 0xff));
    put(fb, x, 12, buf, DBG_LABEL);
    if (d->heap_known) {
        x = put(fb, 73, 12, "MEM", DBG_LABEL) + 3;
        snprintf(buf, sizeof(buf), "%u%%", d->heap_used);
        x = put(fb, x, 12, buf, d->heap_used > 90 ? FX_RED : DBG_TEXT) + 3;
        snprintf(buf, sizeof(buf), "^%u", d->heap_peak);
        put(fb, x, 12, buf, d->heap_peak > 95 ? FX_RED : DBG_LABEL);
    }
    rule(fb, 21);

    /* the links */
    put_right(fb, DBG_COL_LEVEL, 23, "BAT", DBG_LABEL);
    put_right(fb, DBG_COL_RSSI, 23, "DBM", DBG_LABEL);
    put_right(fb, DBG_COL_INTERVAL, 23, "MS", DBG_LABEL);
    put_right(fb, DBG_COL_LATENCY, 23, "LT", DBG_LABEL);
    put_right(fb, DBG_COL_DROPS, 23, "DR", DBG_LABEL);
    put_right(fb, DBG_COL_REASON, 23, "RS", DBG_LABEL);
    put_right(fb, DBG_COL_AGE, 23, "S", DBG_LABEL);
    for (int half = 0; half < 2; half++) {
        const uint8_t level = in->status ? in->status->battery[half] : 0;
        if (level) {
            snprintf(buf, sizeof(buf), "%u", level);
        } else {
            strcpy(buf, "--");
        }
        debug_link(fb, 31 + half * 8, "LR"[half], &d->half[half], buf,
                   level == 0 ? DBG_LABEL : level <= 15 ? FX_RED : DBG_TEXT, level ? d->battery_age_s[half] : -1);
    }
    const bool usb_out = in->status && in->status->usb;
    snprintf(buf, sizeof(buf), "BT%u", in->status ? in->status->profile : 0);
    debug_link(fb, 47, 'H', &d->host, usb_out ? "USB" : buf, usb_out ? DBG_LABEL : DBG_TEXT, -1);
    rule(fb, 56);

    /* the five profiles: a filled circle is connected, a ring paired, a
     * dot free; the one in use is underlined. Then USB and the output. */
    for (int i = 0; i < FX_DEBUG_PROFILES; i++) {
        static const char *const shapes[3][5] = {{".....", ".....", "..o..", ".....", "....."},
                                                 {".ooo.", "o...o", "o...o", "o...o", ".ooo."},
                                                 {".ooo.", "ooooo", "ooooo", "ooooo", ".ooo."}};
        const int state = d->profile_state[i] > 2 ? 2 : d->profile_state[i], cx = 6 + i * 8;
        const bool active = in->status && in->status->profile == i;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 5; col++) {
                if (shapes[state][row][col] == 'o') {
                    plot(fb, cx + col, 59 + row, state == 2 ? DBG_GOOD : state == 1 ? DBG_TEXT : DBG_LABEL);
                }
            }
        }
        if (active) {
            line(fb, cx, 65, cx + 4, 65, DBG_BRIGHT);
        }
    }
    x = put(fb, 50, 59, "USB", DBG_LABEL) + 3;
    x = put(fb, x, 59, d->usb == 2 ? "HID" : d->usb == 1 ? "PWR" : "--", d->usb == 2 ? DBG_GOOD : DBG_TEXT) + 6;
    x = put(fb, x, 59, "OUT", DBG_LABEL) + 3;
    put(fb, x, 59, usb_out ? "USB" : "BLE", in->status && in->status->host_connected ? DBG_GOOD : FX_RED);

    /* layer (number, name, all that are on), typing speed */
    snprintf(buf, sizeof(buf), "L%u", d->layer);
    x = put(fb, 5, 68, buf, DBG_BRIGHT) + 3;
    if (d->layer_name) {
        snprintf(buf, sizeof(buf), "%.7s", d->layer_name);
        for (char *c = buf; *c; c++) {
            *c = *c >= 'a' && *c <= 'z' ? *c - 32 : *c; /* the small letters are for typed text */
        }
        x = put(fb, x, 68, buf, DBG_TEXT) + 3;
    }
    snprintf(buf, sizeof(buf), "%04X", (unsigned)(d->layers & 0xffff));
    put(fb, x, 68, buf, DBG_LABEL);
    snprintf(buf, sizeof(buf), "%uWPM", d->wpm);
    put_right(fb, FX_W - 4, 68, buf, DBG_TEXT);

    /* modifiers, lit while held (either side), and the presses counted */
    static const char *const mods[] = {"CTL", "SFT", "ALT", "GUI"};
    for (int i = 0; i < 4; i++) {
        const bool on = (d->mods & (0x11 << i)) != 0;
        if (on) {
            for (int dy = -1; dy <= 7; dy++) {
                line(fb, 4 + i * 20, 76 + dy, 4 + i * 20 + 16, 76 + dy, DBG_TEXT);
            }
        }
        put(fb, 5 + i * 20, 76, mods[i], on ? DBG_BG : DBG_LABEL);
    }
    snprintf(buf, sizeof(buf), "N%u", (unsigned)(d->presses % 1000000));
    put_right(fb, FX_W - 4, 76, buf, DBG_TEXT);
    rule(fb, 85);

    /* the last raw events, the newest at the bottom and brightest */
    for (int i = 0; i < d->n_log && i < FX_DEBUG_LOG; i++) {
        const struct fx_event *e = &d->log[i];
        const char who = e->source == 0 ? 'L' : e->source == 1 ? 'R' : ' ';
        int n = snprintf(buf, sizeof(buf), "+%04u ", e->gap_ms > 9999 ? 9999 : e->gap_ms);
        if (e->kind == 'P') {
            snprintf(buf + n, sizeof(buf) - n, "P%02u%c %c", e->a, e->down ? 'v' : '^', who);
        } else if (e->kind == 'K') {
            snprintf(buf + n, sizeof(buf) - n, "K%X:%02X%c %02X", e->a & 0xf, e->b & 0xff, e->down ? 'v' : '^', e->mods);
        } else {
            snprintf(buf + n, sizeof(buf) - n, "E%u %+d %c", e->a, (int16_t)e->b, who);
        }
        put(fb, 5, 88 + i * 8, buf, i == d->n_log - 1 ? DBG_BRIGHT : DBG_TEXT);
    }

    /* the keys: white while held, glowing for a moment after */
    for (int k = 0; k < in->n_keys; k++) {
        if (in->keys[k] < sizeof(debug_heat)) {
            debug_heat[in->keys[k]] = 15;
        }
    }
    for (int key = 0; key < 34; key++) {
        /* the four thumb keys sit under the middle of the board */
        const int col = key_row(key) == 3 ? key_col(key) + 3 : key_col(key);
        const int kx = 88 + col * 5 + (col >= 5 ? 1 : 0), ky = 88 + key_row(key) * 5;
        const bool held = (d->held >> key) & 1;
        const uint8_t color = held ? DBG_BRIGHT : debug_heat[key] ? DBG_HEAT + debug_heat[key] : DBG_DIM;
        for (int i = 0; i < 16; i++) {
            plot(fb, kx + (i & 3), ky + (i >> 2), color);
        }
    }
    debug_fade += in->step;
    if (debug_fade >= 2) { /* the glow is gone in a second */
        debug_fade = 0;
        for (unsigned key = 0; key < sizeof(debug_heat); key++) {
            debug_heat[key] -= debug_heat[key] > 0;
        }
    }

    /* the quickest a key has come back down after going up: a switch that
     * bounces shows here as a few milliseconds */
    x = put(fb, 14, 112, "BNC", DBG_LABEL) + 3;
    if (d->bounce_ms) {
        snprintf(buf, sizeof(buf), "%uMS P%02u", d->bounce_ms, d->bounce_key);
        put(fb, x, 112, buf, d->bounce_ms < 30 ? FX_RED : DBG_TEXT);
    } else {
        put(fb, x, 112, "--", DBG_LABEL);
    }
}

/* --- the list --------------------------------------------------------------- */

#define RGB565(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))
#define LOW_RED RGB565(255, 70, 50)

static const struct {
    const char *name;
    void (*start)(uint16_t *palette);
    void (*frame)(uint8_t *fb, const struct fx_input *in);
    struct fx_theme theme; /* the status text over it: see fx.h */
    bool debug;            /* see fx_is_debug */
} effects[] = {
    {"WARP", warp_start, warp_frame,
     {.ink = RGB565(255, 255, 255), .dim = RGB565(160, 160, 160), .accent = RGB565(40, 240, 230),
      .search = RGB565(208, 208, 208), .low = LOW_RED, .halo = 0}, false},
    /* the flames are behind the text when typing, and nearly white: dark
     * text, with a glow round it for when they have died down */
    {"FIRE", fire_start, fire_frame,
     {.ink = RGB565(0, 0, 0), .dim = RGB565(90, 40, 0), .accent = RGB565(0, 0, 0),
      .search = RGB565(120, 60, 20), .low = RGB565(190, 0, 0), .halo = RGB565(255, 230, 140)}, false},
    {"TERMINAL", term_start, term_frame,
     {.ink = RGB565(40, 255, 90), .dim = RGB565(0, 130, 45), .accent = RGB565(215, 255, 225),
      .search = RGB565(120, 160, 130), .low = LOW_RED, .halo = 0}, false},
    {"CODE", code_start, code_frame,
     {.ink = RGB565(220, 255, 225), .dim = RGB565(110, 170, 145), .accent = RGB565(60, 230, 255),
      .search = RGB565(150, 180, 165), .low = LOW_RED, .halo = 0}, false},
    {"THERMAL", thermal_start, thermal_frame,
     {.ink = RGB565(255, 255, 255), .dim = RGB565(170, 170, 170), .accent = RGB565(255, 255, 255),
      .search = RGB565(190, 190, 190), .low = RGB565(255, 60, 60), .halo = 0}, false},
    {"TRACKER", tracker_start, tracker_frame,
     {.ink = RGB565(150, 220, 245), .dim = RGB565(60, 120, 150), .accent = RGB565(235, 250, 255),
      .search = RGB565(110, 150, 170), .low = LOW_RED, .halo = RGB565(0, 5, 14)}, false},
    {"LIFE", life_start, life_frame,
     {.ink = RGB565(255, 255, 255), .dim = RGB565(160, 160, 160), .accent = RGB565(60, 235, 130),
      .search = RGB565(208, 208, 208), .low = LOW_RED, .halo = 0}, false},
    {"HOP", hop_start, hop_frame,
     {.ink = RGB565(255, 255, 255), .dim = RGB565(215, 232, 250), .accent = RGB565(255, 215, 40),
      .search = RGB565(215, 232, 250), .low = RGB565(255, 90, 120), .halo = RGB565(70, 35, 30)}, false},
    {"DEBUG", debug_start, debug_frame, {0}, true},
};
#define FX_COUNT ((int)(sizeof(effects) / sizeof(effects[0])))

int fx_count(void) { return FX_COUNT; }

const char *fx_name(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].name : ""; }

bool fx_is_debug(int fx) { return fx >= 0 && fx < FX_COUNT && effects[fx].debug; }

const struct fx_theme *fx_theme(int fx) { return &effects[fx >= 0 && fx < FX_COUNT ? fx : 0].theme; }

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

/* --- the name ---------------------------------------------------------------- */

void fx_draw_banner(uint8_t *fb, const char *banner) {
    if (banner) {
        text(fb, 10, 9, banner, FX_WHITE);
    }
}
