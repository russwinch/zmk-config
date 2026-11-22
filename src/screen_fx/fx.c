/*
 * Animations for the Prospector dongle's screen: see fx.h.
 *
 * Each animation is a start function (palette, state) and a frame function.
 * `canvas` is one spare picture they share for whatever has to survive from
 * frame to frame (heat, fading trails); the frame handed in is redrawn
 * completely every time. The
 * caller owns the canvas's memory (fx_set_canvas): the dongle has none to
 * spare for it.
 */

#include "fx.h"
#include "colors.h"

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

int fx_corner_inset(int y0, int y1) {
    int inset = 0;
    for (int y = y0; y <= y1; y++) {
        /* how far the middle of the row is into a corner's curve */
        const float yc = y + 0.5f, top = FX_CORNER_R - yc, bottom = yc - (FX_H - FX_CORNER_R);
        const float into = top > bottom ? top : bottom;
        if (into <= 0) {
            continue;
        }
        if (into >= FX_CORNER_R) {
            return FX_W / 2;
        }
        /* the first pixel whose middle is on the panel, and one more for the
         * radius being a measurement of another maker's glass */
        const float edge = FX_CORNER_R - sqrtf(FX_CORNER_R * FX_CORNER_R - into * into);
        const int need = (int)ceilf(edge - 0.5f) + 1;
        inset = need > inset ? need : inset;
    }
    return inset;
}

static inline uint16_t rgb(int r, int g, int b) {
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static const struct scheme *scheme = &schemes[0];

int fx_scheme_count(void) { return SCHEMES; }

const char *fx_scheme_name(int which) { return which >= 0 && which < SCHEMES ? schemes[which].name : ""; }

void fx_set_scheme(int which) { scheme = &schemes[which >= 0 && which < SCHEMES ? which : 0]; }

/* `k` 256ths of the way from `a` to `b`. */
static struct color mix(struct color a, struct color b, int k) {
    return (struct color){(uint8_t)(a.r + (b.r - a.r) * k / 256), (uint8_t)(a.g + (b.g - a.g) * k / 256),
                          (uint8_t)(a.b + (b.b - a.b) * k / 256)};
}

static inline uint16_t color565(struct color c) { return rgb(c.r, c.g, c.b); }

/* --- the colour theme that is set: colors.h has the list ------------------- */

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

/* --- the keys --------------------------------------------------------------- */

/* Where each key position is on the board (fx_set_keys). Until the dongle
 * says, the Urchin and Forager: three rows of ten, two thumb keys a side. */
#define RC(row, col) ((row) << 8 | (col))
static const uint16_t urchin_keys[] = {
    RC(0, 0), RC(0, 1), RC(0, 2), RC(0, 3), RC(0, 4), RC(0, 5), RC(0, 6), RC(0, 7), RC(0, 8), RC(0, 9),
    RC(1, 0), RC(1, 1), RC(1, 2), RC(1, 3), RC(1, 4), RC(1, 5), RC(1, 6), RC(1, 7), RC(1, 8), RC(1, 9),
    RC(2, 0), RC(2, 1), RC(2, 2), RC(2, 3), RC(2, 4), RC(2, 5), RC(2, 6), RC(2, 7), RC(2, 8), RC(2, 9),
    RC(3, 3), RC(3, 4), RC(3, 5), RC(3, 6),
};
#undef RC
static const uint16_t *key_places = urchin_keys;
static int key_count = sizeof(urchin_keys) / sizeof(urchin_keys[0]);
static int board_cols = 10;

void fx_set_keys(const uint16_t *places, int count) {
    if (places == NULL || count <= 0) {
        return;
    }
    key_places = places;
    key_count = count;
    board_cols = 2;
    for (int i = 0; i < count; i++) {
        const int cols = (places[i] & 0xff) + 1;
        board_cols = cols > board_cols ? cols : board_cols;
    }
    board_cols += board_cols & 1; /* the halves are the same width */
}

/* A key's own column and row on its board; a position the board hasn't got
 * counts as the middle of the bottom row. */
static inline int board_col(int key) { return key < key_count ? key_places[key] & 0xff : board_cols / 2; }
static inline int board_row(int key) { return key < key_count ? key_places[key] >> 8 : 3; }

/* The key positions that are an encoder's push, by sensor (fx_set_encoders). */
static int8_t encoder_keys[FX_MAX_ENCODERS];
static int encoder_count;

void fx_set_encoders(const int8_t *keys, int count) {
    encoder_count = 0;
    for (int i = 0; keys != NULL && i < count && i < FX_MAX_ENCODERS; i++) {
        encoder_keys[encoder_count++] = keys[i];
    }
}

/* The encoder a key position is the push of, or -1. */
static int key_encoder(int key) {
    for (int i = 0; i < encoder_count; i++) {
        if (encoder_keys[i] == key) {
            return i;
        }
    }
    return -1;
}

/* The same for the animations, which take every board for 10 columns by 4
 * rows: on a wider one the outer columns count as the ones next to them, so
 * each half's keys stay on its side. */
static inline int key_col(uint8_t key) {
    const int col = board_col(key) - (board_cols - 10) / 2;
    return col < 0 ? 0 : col > 9 ? 9 : col;
}
static inline int key_row(uint8_t key) { return board_row(key) < 3 ? board_row(key) : 3; }

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

static const uint8_t bolt[7] = {0x02, 0x06, 0x0c, 0x1f, 0x06, 0x0c, 0x08};
/* ⌃ ⌥ ⇧ ⌘, as FX_GLYPH_CTRL to FX_GLYPH_GUI */
static const uint8_t mod_glyphs[4][7] = {
    {0x00, 0x00, 0x04, 0x0a, 0x11, 0x00, 0x00},
    {0x00, 0x1b, 0x04, 0x04, 0x02, 0x03, 0x00},
    {0x04, 0x0a, 0x11, 0x1b, 0x0a, 0x0e, 0x00},
    {0x1b, 0x1f, 0x0a, 0x1f, 0x1b, 0x00, 0x00},
};

const uint8_t *fx_glyph_rows(char c) {
    if (c == FX_GLYPH_BOLT) {
        return bolt;
    }
    if (c >= FX_GLYPH_CTRL && c <= FX_GLYPH_GUI) {
        return mod_glyphs[c - FX_GLYPH_CTRL];
    }
    return c < ' ' || c > '~' ? NULL : font[c - ' '];
}

static void glyph(uint8_t *fb, int x, int y, char c, uint8_t color) {
    const uint8_t *rows = fx_glyph_rows(c);
    if (rows == NULL) {
        return;
    }
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row] & (0x10 >> col)) {
                plot(fb, x + col, y + row, color);
            }
        }
    }
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
    for (int h = 0; h < 4; h++) {
        gradient(palette, &warp_stops[h * 4], 4);
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

/* --- terminal: what you type, in green, fading away ------------------------ */

#define TERM_COLS 25
#define TERM_ROWS 10 /* down to just above the status text */
#define TERM_X 7     /* the top line clear of the corners' curve */
#define TERM_Y 6
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

/* The theme's colours: its background, then what is typed fading in
 * reverse, dim to text, with bright for its first flash. */
static void term_colors(uint16_t *palette) {
    const struct color at[] = {scheme->dim, scheme->text, scheme->bright};
    const struct stop stops[] = {{0, scheme->bg.r, scheme->bg.g, scheme->bg.b},
                                 {70, at[0].r, at[0].g, at[0].b},
                                 {TERM_LIT, at[1].r, at[1].g, at[1].b},
                                 {251, at[2].r, at[2].g, at[2].b}};
    gradient(palette, stops, 4);
}

static void term_start(uint16_t *palette) {
    (void)palette;
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
 * Lines of made-up code, with indents, words and comments, written down the
 * page from the top as a terminal's are; once the page is full each new
 * line pushes the rest up and off the top. It writes slowly on its own; each
 * key adds two characters (the same key always gives the same one), so at
 * speed the page races. The letters are drawn up afresh from a few strokes
 * each time the firmware starts.
 */

#define CODE_COLS 27
#define CODE_ROWS 14
#define CODE_PITCH 7  /* rows; characters are 5 apart */
#define CODE_GLYPHS 64 /* 0 is the space */
#define CODE_LEVELS 63 /* palette: 4 colours x 63 brightness levels */
#define CODE_BOTTOM 86 /* top of the last line of the page */

static struct {
    uint8_t glyph, hue, heat;
} code_cell[CODE_ROWS][CODE_COLS];
static uint32_t code_glyphs[CODE_GLYPHS]; /* 4x5 dots */
static int code_row, code_col, code_indent, code_line_end, code_word, code_hue;
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
    if (code_row < CODE_ROWS - 1) {
        code_row++;
    } else {
        memmove(code_cell[0], code_cell[1], sizeof(code_cell) - sizeof(code_cell[0]));
        memset(code_cell[CODE_ROWS - 1], 0, sizeof(code_cell[0]));
        code_scroll = CODE_PITCH;
    }

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
    code_cell[code_row][code_col].glyph = glyph;
    code_cell[code_row][code_col].hue = (uint8_t)code_hue;
    code_cell[code_row][code_col].heat = CODE_LEVELS - 1;
    code_col++;
}

/* An empty page, about to be written on. */
static void code_clear(void) {
    memset(code_cell, 0, sizeof(code_cell));
    code_indent = 0;
    code_pending = code_scroll = 0;
    code_row = 0; /* row 0 is off the top of the screen: the first line is row 1 */
    code_new_line();
}

static void code_start(uint16_t *palette) {
    for (int h = 0; h < 4; h++) {
        gradient(palette, &code_stops[h * 4], 4);
    }
    if (code_glyphs[1] == 0) {
        code_make_glyphs();
    }
    code_clear();
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
        if (r > code_row) {
            break;
        }
        int base = 46 - 2 * (code_row - r); /* dimmer on the way up */
        /* a letter the corners' curve would cut into isn't written there */
        const int inset = fx_corner_inset(top + 1, top + 5);
        for (int c = 0; c < CODE_COLS; c++) {
            if (code_cell[r][c].glyph == 0) {
                continue;
            }
            /* written white hot, cooling to its colour */
            int heat = code_cell[r][c].heat;
            code_cell[r][c].heat = heat > cool ? heat - cool : 0;
            if (3 + c * 5 < inset || 3 + c * 5 + 3 >= FX_W - inset) {
                continue;
            }
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
    /* the cursor, unless it is in a corner's curve as the letters aren't */
    const int cursor_x = 3 + (code_col < CODE_COLS ? code_col : CODE_COLS - 1) * 5;
    const int cursor_y = CODE_BOTTOM - (CODE_ROWS - 1 - code_row) * CODE_PITCH + (int)code_scroll;
    const int cursor_inset = fx_corner_inset(cursor_y, cursor_y + 5);
    for (int y = 0; y < 6 && cursor_x >= cursor_inset && cursor_x + 3 < FX_W - cursor_inset; y++) {
        for (int x = 0; x < 4; x++) {
            plot(fb, cursor_x + x, cursor_y + y, CODE_LEVELS - 1);
        }
    }
}

/* --- thermal: the keyboard through a heat camera ---------------------------- */

/*
 * The screen is the board seen from above: each key leaves a hot spot at its
 * own place, which spreads as it cools from white through red, yellow and
 * green back into the cold blue, slowly enough for the spots of a word to
 * run together. Two warm patches wander about so there is something to see
 * when idle, the whole picture warms up with typing speed, and a sight
 * follows the last key pressed.
 *
 * From THERMAL_FIRE_WPM the board catches: each key burns for a second or
 * two after it is pressed, as a heat camera shows a flame: a white core that
 * flickers and cools quickly, and above it the smoke, a faint warm plume
 * that drifts and widens on its way up.
 *
 * A key held down stays as hot as pressing it made it and goes on getting
 * hotter and wider, and after THERMAL_HOLD_S catches by itself, at any
 * speed, and burns until it is let go.
 *
 * The canvas holds two pictures: the board's heat at half size (it is smooth
 * anyway, and spreads further for the same work), and the flames and smoke
 * at full size over the rows the board takes up (GAS_H of them, from
 * GAS_TOP).
 */

#define THERMAL_FIRE_WPM 50
#define THERMAL_HOLD_S 5
#define THERMAL_KEYS 64  /* key positions that can be told to be held */
#define HOLD_TICKS (THERMAL_HOLD_S * 30) /* thermal_held counts 30ths of a second */
#define HEAT_W (FX_W / 2)
#define HEAT_H (FX_H / 2)
#define HEAT_CELLS (HEAT_W * HEAT_H)
#define HEAT_MAX 226    /* as hot as the board shows: white is the flames' */
#define GAS_H 90        /* rows of flame and smoke... */
#define GAS_TOP 6       /* ...from this one down: the bottom row of keys is inside, with room over the top row */
#define PRESS_HEAT 205  /* what a key press brings its spot up to: red hot */
#define GAS_SMOKE 85    /* under this it is smoke, which cools slowly */
#define GAS_LINGER 80   /* frames the smoke gets to clear once the fire is out */
#define BURNERS 12      /* keys alight at once */

_Static_assert(HEAT_CELLS + FX_W * GAS_H <= PIXELS, "the canvas holds both");

static float thermal_time, thermal_x, thermal_y, thermal_lock, thermal_blaze;
static int thermal_tx, thermal_ty, thermal_frames, thermal_smoke, thermal_next;
static struct {
    uint8_t x, y, life; /* life: frames left to burn */
} burners[BURNERS];
static uint16_t thermal_held[THERMAL_KEYS]; /* how long each key has been down */

static void thermal_start(uint16_t *palette) {
    gradient(palette, thermal_stops, 7);
    thermal_time = thermal_blaze = 0;
    thermal_x = thermal_tx = FX_W / 2;
    thermal_y = thermal_ty = FX_H / 2 - 5;
    thermal_lock = 0;
    thermal_frames = thermal_smoke = 0;
    memset(burners, 0, sizeof(burners));
    memset(thermal_held, 0, sizeof(thermal_held));
}

/* Warm a picture `w` by `h` around (cx, cy): `peak` in the middle, nothing at
 * `radius`. Added to what is there, or with `hold` only bringing it up to
 * that. */
static void glow_spot_in(uint8_t *buf, int w, int h, int cx, int cy, int radius, int peak, bool hold) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int far = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (far >= radius * radius || x < 0 || x >= w || y < 0 || y >= h) {
                continue;
            }
            int was = buf[y * w + x], v = peak * (radius * radius - far) / (radius * radius);
            v = hold ? (v > was ? v : was) : v + was;
            buf[y * w + x] = v > 251 ? 251 : v;
        }
    }
}

/* The same, on the whole canvas. */
static void glow_spot(int cx, int cy, int radius, int peak, bool hold) {
    glow_spot_in(canvas, FX_W, FX_H, cx, cy, radius, peak, hold);
}

/* A battery: a gauge from y 16 to 90 at `x`, on `back`. */
static void side_battery(uint8_t *fb, int x, uint8_t percent, bool connected, uint8_t edge, uint8_t full,
                         uint8_t back) {
    const int top = 16, bottom = 90;
    const bool low = !connected || (percent > 0 && percent <= FX_LOW_BATTERY);
    int fill = 0;
    if (connected && percent > 0) {
        fill = (bottom - top - 1) * (percent > 100 ? 100 : percent) / 100;
    }
    for (int y = top; y <= bottom; y++) {
        for (int dx = -1; dx <= 5; dx++) {
            const bool side = y == top || y == bottom || dx == 0 || dx == 4;
            const bool inside = dx > 0 && dx < 4 && y > bottom - 1 - fill;
            /* a gap every tenth of the way up */
            const bool gap = (bottom - y) % 8 == 0;
            plot(fb, x + dx, y,
                 dx < 0 || dx > 4 ? back : side ? edge : inside && !gap ? (low ? FX_RED : full) : back);
        }
    }
}

/* One step left or right, or (half the time) neither, from two random bits. */
static inline int sidestep(uint32_t bits) { return (bits & 3) == 0 ? -1 : (bits & 3) == 3 ? 1 : 0; }

/* Where a key is: the board fills the screen, the halves apart, with the
 * bottom row between the two battery levels. */
static void thermal_place(int key, int *x, int *y) {
    const int col = key_col((uint8_t)key);
    *x = 9 + col * 12 + (col >= 5 ? 14 : 0);
    *y = 20 + key_row((uint8_t)key) * 23;
}

/* A flame's foot, at a key (where it is on the screen): a flickering spot,
 * bigger with `life`. */
static void thermal_flame(uint8_t *gas, int x, int y, int life) {
    const uint32_t r = rnd();
    y -= GAS_TOP;
    int peak = 130 + life * 7 - (int)((r >> 4) & 63);
    peak = peak > 251 ? 251 : peak;
    if (peak > 0) {
        glow_spot_in(gas, FX_W, GAS_H, x + sidestep(r) * 2, y - 2 + sidestep(r >> 2), life > 18 ? 5 : life > 6 ? 4 : 3,
                     peak, true);
    }
}

/* The flames and the smoke. Everything rises: a pixel takes the heat of two
 * a row or three below it, a step to either side, less what it lost on the
 * way: a lot while it is flame, little once it is smoke. */
static void thermal_burn(uint8_t *gas) {
    int x, y;

    /* what was at the bottom has gone up */
    memset(gas + (GAS_H - 1) * FX_W, 0, FX_W);

    const int flame_cool = 19 - (int)(thermal_blaze * 8); /* taller flames at speed */
    for (int y = 0; y < GAS_H - 1; y++) {
        uint8_t *row = &gas[y * FX_W];
        /* the air sways: bands of it leaning one way and the other, going up */
        int lean = (y * 2 + (int)(thermal_time * 0.9f)) % 96;
        lean = (lean < 48 ? lean : 96 - lean) - 24; /* -24..24 */
        const int lean_to = lean < 0 ? -1 : 1, lean_odds = (lean < 0 ? -lean : lean) / 3; /* in 16 */
        const int below = GAS_H - 1 - y;
        for (int x = 0; x < FX_W; x++) {
            const uint32_t r = rnd();
            const int drift = (int)((r >> 28) & 15) < lean_odds ? lean_to : 0;
            int ya = 1 + (int)((r >> 4) & 1), yb = 1 + (int)((r >> 5) % 3);
            int xa = x + sidestep(r) + drift, xb = x + sidestep(r >> 2) * 2 + drift;
            ya = ya > below ? below : ya;
            yb = yb > below ? below : yb;
            xa = (unsigned)xa < FX_W ? xa : x;
            xb = (unsigned)xb < FX_W ? xb : x;
            int v = (row[ya * FX_W + xa] + row[yb * FX_W + xb] + 1) >> 1;
            if (v == 0) {
                row[x] = 0;
                continue;
            }
            const int chance = (int)((r >> 8) & 0xfff);
            v -= v > GAS_SMOKE ? chance % flame_cool : chance % 3;
            row[x] = v > 0 ? (uint8_t)v : 0;
        }
    }

    /* the keys alight: each a flickering spot, dying down */
    for (int i = 0; i < BURNERS; i++) {
        const int life = burners[i].life;
        if (life == 0) {
            continue;
        }
        burners[i].life--;
        thermal_flame(gas, burners[i].x, burners[i].y, life);
    }
    /* and the ones held down long enough: small at first, full in a second */
    for (int key = 0; key < THERMAL_KEYS && key < key_count; key++) {
        if (thermal_held[key] >= HOLD_TICKS) {
            const int life = 8 + (thermal_held[key] - HOLD_TICKS);
            thermal_place(key, &x, &y);
            thermal_flame(gas, x, y, life > 30 ? 30 : life);
        }
    }
}

static void thermal_frame(uint8_t *fb, const struct fx_input *in) {
    uint8_t *heat = canvas, *gas = canvas + HEAT_CELLS;
    thermal_time += in->step;
    thermal_frames++;

    /* the fire: from THERMAL_FIRE_WPM, growing with speed. The figure
     * moves once a second, so it is eased in and out. */
    const float fire = in->wpm >= THERMAL_FIRE_WPM
                           ? clampf(0.35f + (in->wpm - THERMAL_FIRE_WPM) / 45.0f, 0.0f, 1.0f)
                           : 0.0f;
    thermal_blaze += (fire - thermal_blaze) * clampf(0.06f * in->step, 0.0f, 1.0f);
    const bool alight = thermal_blaze > 0.05f;

    for (int k = 0; k < in->n_keys; k++) {
        thermal_place(in->keys[k], &thermal_tx, &thermal_ty);
        /* up to red hot, and a little more each time: a key hit over and
         * over glows wider, but doesn't swamp the board */
        glow_spot_in(heat, HEAT_W, HEAT_H, thermal_tx / 2, thermal_ty / 2, 5, PRESS_HEAT, true);
        glow_spot_in(heat, HEAT_W, HEAT_H, thermal_tx / 2, thermal_ty / 2, 6, 30, false);
        if (alight) {
            thermal_next = (thermal_next + 1) % BURNERS;
            burners[thermal_next].x = (uint8_t)thermal_tx;
            burners[thermal_next].y = (uint8_t)thermal_ty;
            burners[thermal_next].life = (uint8_t)(14 + thermal_blaze * 22);
        }
        thermal_lock = 1;
    }
    /* a key held down: from as hot as the press left it, hotter and wider
     * the longer it is held, never cooler */
    bool held_alight = false;
    for (int key = 0; key < THERMAL_KEYS && key < key_count; key++) {
        if (!(in->held >> key & 1)) {
            thermal_held[key] = 0;
            continue;
        }
        const int ticks = thermal_held[key] + (int)(in->step + 0.5f);
        thermal_held[key] = (uint16_t)(ticks > 60000 ? 60000 : ticks);
        const int warmth = ticks < HOLD_TICKS ? ticks * 100 / HOLD_TICKS : 100; /* percent */
        int x, y;
        thermal_place(key, &x, &y);
        glow_spot_in(heat, HEAT_W, HEAT_H, x / 2, y / 2, 5 + warmth / 34, PRESS_HEAT + warmth * 2 / 5, true);
        held_alight = held_alight || ticks >= HOLD_TICKS;
    }

    /* the wanderers, warmer the faster you type */
    int warm = 60 + (int)(in->energy * 70);
    glow_spot_in(heat, HEAT_W, HEAT_H, HEAT_W / 2 + (int)(sinf(thermal_time * 0.013f) * 28),
                 27 + (int)(sinf(thermal_time * 0.021f + 2) * 17), 7, warm, true);
    glow_spot_in(heat, HEAT_W, HEAT_H, HEAT_W / 2 + (int)(sinf(thermal_time * 0.017f + 4) * 27),
                 30 + (int)(sinf(thermal_time * 0.011f) * 18), 6, warm, true);

    /* heat spreads to the cells around, half of it a frame, and leaks away a
     * little at a time. Worked through forwards and backwards by turns, so
     * that it doesn't creep one way. */
    const bool backwards = thermal_frames & 1;
    uint32_t r = 0;
    for (int n = 0; n < HEAT_H - 2; n++) {
        uint8_t *row = &heat[(backwards ? HEAT_H - 2 - n : 1 + n) * HEAT_W];
        for (int m = 0; m < HEAT_W - 2; m++) {
            const int x = backwards ? HEAT_W - 2 - m : 1 + m;
            if ((m & 3) == 0) {
                r = rnd();
            }
            int v = (4 * row[x] + row[x - 1] + row[x + 1] + row[x - HEAT_W] + row[x + HEAT_W] + 4) >> 3;
            v -= (v + (int)(r & 127)) >> 7;
            r >>= 7;
            row[x] = (uint8_t)v;
        }
    }
    /* the cells round the edge aren't worked through: they take after the
     * ones inside them, or what a key by the edge put there would stay */
    for (int y = 1; y < HEAT_H - 1; y++) {
        heat[y * HEAT_W] = heat[y * HEAT_W + 1];
        heat[y * HEAT_W + HEAT_W - 1] = heat[y * HEAT_W + HEAT_W - 2];
    }
    memcpy(heat, heat + HEAT_W, HEAT_W);
    memcpy(heat + (HEAT_H - 1) * HEAT_W, heat + (HEAT_H - 2) * HEAT_W, HEAT_W);

    if (alight || held_alight) {
        thermal_smoke = GAS_LINGER;
    }
    if (thermal_smoke > 0) {
        thermal_smoke--;
        thermal_burn(gas);
        if (thermal_smoke == 0) {
            memset(gas, 0, FX_W * GAS_H);
        }
    }

    /* the picture: the heat, and the flames over it, on a cold, noisy
     * background that typing warms */
    int ambient = 12 + (int)(in->energy * 28);
    int band = (int)(thermal_time * 0.8f) % (FX_H + 40) - 20; /* a brighter line rolling down */
    for (int y = 0; y < FX_H; y++) {
        int lift = ambient + (y > band - 3 && y < band + 3 ? 7 : 0);
        /* each cell is two pixels each way: the ones between are averaged */
        const uint8_t *h0 = &heat[(y >> 1) * HEAT_W];
        const uint8_t *h1 = (y & 1) && (y >> 1) + 1 < HEAT_H ? h0 + HEAT_W : h0;
        const bool burning = thermal_smoke > 0 && y >= GAS_TOP && y < GAS_TOP + GAS_H;
        for (int x = 0; x < FX_W; x++) {
            if ((x & 3) == 0) {
                r = rnd();
            }
            const int cx = x >> 1, cx1 = (x & 1) && cx + 1 < HEAT_W ? cx + 1 : cx;
            int v = ((h0[cx] + h0[cx1] + h1[cx] + h1[cx1]) >> 2) + lift + (int)(r & 7);
            v = v > HEAT_MAX ? HEAT_MAX : v;
            if (burning) {
                /* over the board, not added to it: only the flame's own
                 * core comes out white, however hot the key under it */
                v += gas[(y - GAS_TOP) * FX_W + x] * (256 - v) >> 8;
            }
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
 * every animation has in its bottom corner: here in the small letters, and
 * moved down clear of the line across the bottom.
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

/* The theme's colours, as one run from its background up to nearly its
 * bright (white on a dark theme, nearly black on a light one). */
static struct color tracker_line(void) { return mix(scheme->dim, scheme->label, 64); }
static struct color tracker_text(void) { return mix(scheme->label, scheme->text, 170); }
static struct color tracker_top(void) { return mix(scheme->text, scheme->bright, 205); }

static void tracker_colors(uint16_t *palette) {
    const struct color at[] = {scheme->bg, tracker_line(), tracker_text(), tracker_top()};
    const struct stop stops[] = {{0, at[0].r, at[0].g, at[0].b},
                                 {TRACK_LINE, at[1].r, at[1].g, at[1].b},
                                 {TRACK_TEXT, at[2].r, at[2].g, at[2].b},
                                 {251, at[3].r, at[3].g, at[3].b}};
    gradient(palette, stops, 4);
}

static void tracker_start(uint16_t *palette) {
    (void)palette;
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
        const struct fx_status *s = in->status;
        side_battery(fb, 5, s->battery[0], s->half_connected[0], TRACK_LINE, TRACK_TEXT, 0);
        side_battery(fb, FX_W - 10, s->battery[1], s->half_connected[1], TRACK_LINE, TRACK_TEXT, 0);
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

static float life_tick;
static int life_count[2], life_settled; /* cells alive one and two generations ago */

static void life_start(uint16_t *palette) {
    gradient(palette, life_stops, 6);
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

/* --- debug: everything the dongle knows, as text ---------------------------- */

/*
 * Not an animation: a page of readings for finding faults with connections,
 * switches and the firmware (src/screen_fx/README.md, "Debug"). Top to bottom: the
 * dongle itself; a table of the three Bluetooth links; the profiles, USB
 * and the output in use; layer, typing speed and modifiers; then the last
 * few raw events beside a map of the keys (`board`), and the worst key
 * bounce seen.
 */

enum {
    DBG_BG, DBG_DIM, DBG_LABEL, DBG_TEXT, DBG_BRIGHT, DBG_GOOD, DBG_WARN, DBG_BAD,
    DBG_MOD,   /* a key holding a modifier down */
    DBG_LAYER, /* a key holding a layer on */
    DBG_HEAT   /* ..DBG_HEAT + 15 */
};

static uint8_t debug_heat[64]; /* how lately each key was pressed */
static float debug_fade;
static uint8_t knob_heat[FX_MAX_ENCODERS]; /* how lately each encoder was turned */
static int16_t knob_seen[FX_MAX_ENCODERS];  /* its `turned` when last drawn */
static float knob_angle[FX_MAX_ENCODERS];   /* where its mark is to be, in degrees on the screen */
static float knob_shown[FX_MAX_ENCODERS];   /* and where it is, catching up a frame at a time */
static bool knob_known;                     /* knob_seen has been read */

/* The theme's colours: the Keys screen's too. */
static void debug_colors(uint16_t *palette) {
    palette[DBG_BG] = color565(scheme->bg);
    palette[DBG_DIM] = color565(scheme->dim);
    palette[DBG_LABEL] = color565(scheme->label);
    palette[DBG_TEXT] = color565(scheme->text);
    palette[DBG_BRIGHT] = color565(scheme->bright);
    palette[DBG_GOOD] = color565(scheme->good);
    palette[DBG_WARN] = color565(scheme->warn);
    palette[DBG_BAD] = color565(scheme->bad);
    palette[DBG_MOD] = color565(scheme->mod);
    palette[DBG_LAYER] = color565(scheme->layer);
    for (int i = 0; i < 16; i++) {
        palette[DBG_HEAT + i] = color565(mix(scheme->dim, scheme->glow, i * 256 / 15));
    }
}

static void debug_start(uint16_t *palette) {
    (void)palette;
    memset(debug_heat, 0, sizeof(debug_heat));
    memset(knob_heat, 0, sizeof(knob_heat));
    memset(knob_angle, 0, sizeof(knob_angle));
    memset(knob_shown, 0, sizeof(knob_shown));
    knob_known = false;
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

/*
 * How a link's signal is coloured, on the Debug and Keys screens. The
 * nRF52840 still hears a packet at about -95 dBm (1M PHY), so green is
 * anything with 20 dB or more in hand, plenty for a hand or a body passing
 * through the path; amber still works but has less to spare; red is within
 * 10 dB, where a dip loses packets (sent again, so later) and then the link.
 */
#define SIGNAL_GOOD_DBM -75 /* above it: green */
#define SIGNAL_WARN_DBM -85 /* above it: amber; red at and below */

static uint8_t signal_color(const struct fx_link *link) {
    return link->rssi > SIGNAL_GOOD_DBM ? DBG_GOOD : link->rssi > SIGNAL_WARN_DBM ? DBG_WARN : DBG_BAD;
}

/* Signal strength as four bars, 7 wide and 7 high: a bar every 10 dB from -90. */
static void signal_bars(uint8_t *fb, int x, int y, const struct fx_link *link) {
    const int rssi = link->rssi;
    const int lit = !link->up || !link->rssi_known ? 0 : rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : rssi > -90 ? 1 : 0;
    const uint8_t color = signal_color(link);
    for (int bar = 0; bar < 4; bar++) {
        const int high = 1 + bar * 2;
        for (int dy = 0; dy < high; dy++) {
            plot(fb, x + bar * 2, y + 6 - dy, bar < lit ? color : DBG_DIM);
        }
    }
}

/* Column right edges of the links table: the readings (signal, battery
 * percent and millivolts), then the link's troubles (drops, the reason for
 * the last, the charging-state reads), then the key presses. */
#define DBG_COL_RSSI 26
#define DBG_COL_LEVEL 43 /* and the charging bolt just right of it */
#define DBG_COL_MV 76
#define DBG_COL_DROPS 88
#define DBG_COL_REASON 100
#define DBG_COL_CHARGE 117
#define DBG_COL_PRESSES 138

/* A knob's mark turns this many times as far as the encoder does: one of the
 * Waterfowl's 18 degree clicks moves it 45, an eighth of the way round. */
#define KNOB_GAIN 2.5f
/* Of the way still to go, how much the mark catches up in a 30th of a
 * second: a click's turn is spread over the next few frames. */
#define KNOB_EASE 0.3f

/*
 * An encoder, as a knob filling the `wide` by `high` cell at (x, y): a disc
 * of `fill` inside a `ring`, with a `mark` on the rim (and, on a knob big
 * enough, a line in to the middle) at `degrees` clockwise from the top.
 */
static void knob(uint8_t *fb, int x, int y, int wide, int high, uint8_t fill, uint8_t ring, uint8_t mark,
                 float degrees) {
    const float r = (wide < high ? wide : high) / 2.0f, cx = x + (wide - 1) / 2.0f, cy = y + (high - 1) / 2.0f;
    const float a = degrees * (TWO_PI / 360);
    const float ux = sinf(a), uy = -cosf(a); /* 0: up */
    for (int py = y; py < y + high; py++) {
        for (int px = x; px < x + wide; px++) {
            const float dx = px - cx, dy = py - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r) {
                continue;
            }
            const bool rim = d2 > (r - 1) * (r - 1);
            const bool on_mark = dx * ux + dy * uy > 0 && fabsf(dx * uy - dy * ux) < 0.75f && (rim || r >= 4);
            plot(fb, px, py, on_mark ? mark : rim ? ring : fill);
        }
    }
}

/*
 * The board this dongle is built for, key for key (fx_set_keys), its top
 * left at (left, top): keys `wide` by `high` with `gap` between them,
 * `pitch` apart across, and `split` more between the halves. A key is white
 * while held (orange if it is holding a modifier down, violet if a layer)
 * and glows for two seconds after. An encoder's push (fx_set_encoders) is a
 * knob instead: filled the same way while pressed, its ring glowing and its
 * mark bright for two seconds after it is turned, the mark moving round with it.
 */
static void board(uint8_t *fb, const struct fx_input *in, int left, int top, int pitch, int high, int gap,
                  int split) {
    const struct fx_debug *d = in->debug;
    for (int k = 0; k < in->n_keys; k++) {
        if (in->keys[k] < sizeof(debug_heat)) {
            debug_heat[in->keys[k]] = 15;
        }
    }
    for (int i = 0; i < encoder_count; i++) {
        if (knob_known && d->turned[i] != knob_seen[i]) {
            knob_heat[i] = 15;
            knob_angle[i] += (int16_t)(d->turned[i] - knob_seen[i]) * KNOB_GAIN; /* wraps as `turned` does */
        }
        knob_seen[i] = d->turned[i];
        const float ease = KNOB_EASE * in->step;
        knob_shown[i] += (knob_angle[i] - knob_shown[i]) * (ease > 1 ? 1 : ease);
        if (knob_shown[i] > 360 || knob_shown[i] < -360) { /* keep both small, the same way round */
            const float whole = knob_shown[i] > 0 ? 360 : -360;
            knob_shown[i] -= whole;
            knob_angle[i] -= whole;
        }
    }
    knob_known = true;
    for (int key = 0; key < key_count && key < (int)sizeof(debug_heat); key++) {
        const int col = board_col(key), row = board_row(key) < 4 ? board_row(key) : 4;
        const int kx = left + col * pitch + (col >= board_cols / 2 ? split : 0), ky = top + row * (high + gap);
        const uint64_t bit = 1ull << key;
        const bool held = (d->held & bit) != 0;
        const uint8_t color = !held                 ? (debug_heat[key] ? DBG_HEAT + debug_heat[key] : DBG_DIM)
                              : d->held_layer & bit ? DBG_LAYER
                              : d->held_mod & bit   ? DBG_MOD
                                                    : DBG_BRIGHT;
        const int encoder = key_encoder(key);
        if (encoder >= 0) {
            const int heat = knob_heat[encoder] > debug_heat[key] ? knob_heat[encoder] : debug_heat[key];
            knob(fb, kx, ky, pitch - gap, high, held || debug_heat[key] ? color : DBG_BG,
                 held ? color : heat ? DBG_HEAT + heat : DBG_DIM,
                 held ? DBG_BG : knob_heat[encoder] ? DBG_BRIGHT : DBG_LABEL, knob_shown[encoder]);
            continue;
        }
        for (int y = 0; y < high; y++) {
            for (int x = 0; x < pitch - gap; x++) {
                plot(fb, kx + x, ky + y, color);
            }
        }
    }
    debug_fade += in->step;
    if (debug_fade >= 4) { /* the glow is gone in two seconds */
        debug_fade = 0;
        for (unsigned key = 0; key < sizeof(debug_heat); key++) {
            debug_heat[key] -= debug_heat[key] > 0;
        }
        for (int i = 0; i < FX_MAX_ENCODERS; i++) {
            knob_heat[i] -= knob_heat[i] > 0;
        }
    }
}

/* A count in at most four characters, rounded down: 999, 1.2K, 12K, 999K, 1.2M, 12M, 999M at most. */
static void count_text(char *buf, size_t size, uint32_t n) {
    if (n < 1000) {
        snprintf(buf, size, "%u", (unsigned)n);
    } else if (n < 10000) {
        snprintf(buf, size, "%u.%uK", (unsigned)(n / 1000), (unsigned)(n / 100 % 10));
    } else if (n < 1000000) {
        snprintf(buf, size, "%uK", (unsigned)(n / 1000));
    } else if (n < 10000000) {
        snprintf(buf, size, "%u.%uM", (unsigned)(n / 1000000), (unsigned)(n / 100000 % 10));
    } else {
        snprintf(buf, size, "%uM", (unsigned)(n / 1000000 > 999 ? 999 : n / 1000000));
    }
}

/* A charging half's bolt, at (x, y): shown unless in the off half of its flash. */
static void charging_bolt(uint8_t *fb, int x, int y, const struct fx_status *status, int half) {
    if (status && status->charging[half] && !status->bolt_off[half]) {
        put(fb, x, y, FX_BOLT, DBG_TEXT);
    }
}

/* The colour of a battery level, as the BAT column has it. */
static uint8_t battery_color(int level) {
    return level <= FX_LOW_BATTERY ? DBG_BAD : level <= FX_WARN_BATTERY ? DBG_WARN : DBG_TEXT;
}

/* A half's level to show (0 not known): while it charges, and until its
 * voltage has settled after, the dongle's estimate (`estimate` set), as
 * ZMK's reads high under the charger; otherwise ZMK's. */
static uint8_t half_level(const struct fx_input *in, int half, bool *estimate) {
    *estimate = in->debug && in->debug->estimating[half];
    return *estimate ? in->debug->battery_estimate[half] : in->status ? in->status->battery[half] : 0;
}

/* One row of the table: `level` is the battery percent, or the output for
 * the computer; `mv` its battery's volts, to the millivolt (the dongle's supply for the
 * computer's row); `presses` the key presses, from that half or (for the
 * computer) both. `off`: the link is down on purpose (the computer's, while
 * typing over USB), so it isn't shown as a fault. */
static void debug_link(uint8_t *fb, int y, char name, const struct fx_link *link, const char *level,
                       uint8_t level_color, const char *mv, uint8_t mv_color, uint32_t presses, const char *reads,
                       uint8_t reads_err, bool off) {
    char buf[12];
    const char label[2] = {name, '\0'};
    put(fb, 5, y, label, link->up || off ? DBG_BRIGHT : DBG_BAD);
    put_right(fb, DBG_COL_LEVEL, y, level, level_color);
    put_right(fb, DBG_COL_MV, y, mv, mv_color);
    if (link->up) {
        if (link->rssi_known) {
            snprintf(buf, sizeof(buf), "%d", link->rssi < -99 ? -99 : link->rssi);
            put_right(fb, DBG_COL_RSSI, y, buf, signal_color(link));
        } else if (link->rssi_err) {
            /* why it couldn't be read, to track down: EH no handle for the
             * link, EB no command buffer, E1-E99 the radio's or Zephyr's error */
            if (link->rssi_err == FX_RSSI_ERR_HANDLE) {
                strcpy(buf, "EH");
            } else if (link->rssi_err == FX_RSSI_ERR_BUFFER) {
                strcpy(buf, "EB");
            } else {
                snprintf(buf, sizeof(buf), "E%u", link->rssi_err > 99 ? 99 : link->rssi_err);
            }
            put_right(fb, DBG_COL_RSSI, y, buf, DBG_BAD);
        }
    } else {
        put_right(fb, DBG_COL_RSSI, y, "--", off ? DBG_LABEL : DBG_BAD);
    }
    snprintf(buf, sizeof(buf), "%d", link->drops > 99 ? 99 : link->drops);
    put_right(fb, DBG_COL_DROPS, y, buf, link->drops ? DBG_WARN : DBG_LABEL);
    if (link->reason) {
        snprintf(buf, sizeof(buf), "%02X", link->reason);
        put_right(fb, DBG_COL_REASON, y, buf, DBG_WARN);
    }
    count_text(buf, sizeof(buf), presses);
    put_right(fb, DBG_COL_PRESSES, y, buf, presses ? DBG_TEXT : DBG_LABEL);
    /* a half's charging state: where the dongle has got with it (see
     * dongle_half_info_progress): OK green, WT/Q/R/S on the way in amber,
     * NF (firmware without it) dim, E and its error or TO in red */
    if (reads && reads[0]) {
        const bool bad = reads[0] == 'E' || reads[0] == 'T';
        if (reads[0] == 'E' && reads_err) {
            snprintf(buf, sizeof(buf), "E%02X", reads_err);
        } else {
            snprintf(buf, sizeof(buf), "%s", reads);
        }
        put_right(fb, DBG_COL_CHARGE, y, buf,
                  bad ? DBG_BAD : reads[0] == 'O' ? DBG_GOOD : reads[0] == 'N' ? DBG_LABEL : DBG_WARN);
    }
}

/* The five Bluetooth profiles, from (x, y): a filled circle is connected, a
 * ring paired, a dot free; the one in use is underlined. */
static void profiles(uint8_t *fb, const struct fx_debug *d, const struct fx_status *status, int x, int y) {
    static const char *const shapes[3][5] = {{".....", ".....", "..o..", ".....", "....."},
                                             {".ooo.", "o...o", "o...o", "o...o", ".ooo."},
                                             {".ooo.", "ooooo", "ooooo", "ooooo", ".ooo."}};
    for (int i = 0; i < FX_DEBUG_PROFILES; i++) {
        const int state = d->profile_state[i] > 2 ? 2 : d->profile_state[i], cx = x + i * 8;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 5; col++) {
                if (shapes[state][row][col] == 'o') {
                    plot(fb, cx + col, y + row, state == 2 ? DBG_GOOD : state == 1 ? DBG_TEXT : DBG_LABEL);
                }
            }
        }
        if (status && status->profile == i) {
            line(fb, cx, y + 6, cx + 4, y + 6, DBG_BRIGHT);
        }
    }
}

/* A raw event as text: the time since the one before, then what it was. */
static void event_text(char *buf, size_t size, const struct fx_event *e) {
    const char who = e->source == 0 ? 'L' : e->source == 1 ? 'R' : ' ';
    const int n = snprintf(buf, size, "+%04u ", e->gap_ms > 9999 ? 9999 : e->gap_ms);
    if (e->kind == 'P') {
        snprintf(buf + n, size - n, "P%02u%c %c", e->a, e->down ? 'v' : '^', who);
    } else if (e->kind == 'K') {
        snprintf(buf + n, size - n, "K%X:%02X%c %02X", e->a & 0xf, e->b & 0xff, e->down ? 'v' : '^', e->mods);
    } else {
        snprintf(buf + n, size - n, "E%u %+d %c", e->a, (int16_t)e->b, who);
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

/* A half's draw: "--" not known; hundredths of a mW (tenths from 10), or
 * while rough one place fewer, after a space for the flashing ~ in front. */
static void drain_text(char *buf, size_t size, int hundredths, bool rough) {
    if (hundredths < 0) {
        snprintf(buf, size, "--");
    } else if (rough) {
        const int tenths = (hundredths + 5) / 10;
        if (tenths < 100) {
            snprintf(buf, size, " %d.%d", tenths / 10, tenths % 10);
        } else {
            snprintf(buf, size, " %d", (tenths + 5) / 10);
        }
    } else if (hundredths < 1000) {
        snprintf(buf, size, "%d.%02d", hundredths / 100, hundredths % 100);
    } else {
        snprintf(buf, size, "%d.%d", hundredths / 100, hundredths / 10 % 10);
    }
}

/* A settled draw's colour, by how long a full battery would last at it
 * (at a nominal 3.7 V): green four weeks or more, amber under a week, red
 * under two days. Rough or not known, plain. */
static uint8_t drain_color(int hundredths, bool rough, uint16_t mah) {
    if (hundredths < 0) {
        return DBG_LABEL;
    }
    if (rough || mah == 0) {
        return DBG_TEXT;
    }
    const uint32_t hours = hundredths ? (uint32_t)mah * 370 / (uint32_t)hundredths : UINT32_MAX;
    return hours >= 28 * 24 ? DBG_GOOD : hours >= 7 * 24 ? DBG_TEXT : hours >= 2 * 24 ? DBG_WARN : DBG_BAD;
}

static void debug_frame(uint8_t *fb, const struct fx_input *in) {
    static const struct fx_debug none;
    const struct fx_debug *d = in->debug ? in->debug : &none;
    char buf[32];

    memset(fb, DBG_BG, PIXELS);

    /* the dongle: which build it is, up time, activity, this screen's speed; why it last
     * restarted and how full LVGL's memory is. The top line is in the
     * corners' curve, so it starts further in and ends as far from the right */
    const int top_inset = fx_corner_inset(5, 11);
    int x = put(fb, top_inset, 5, d->firmware ? d->firmware : "DBG", DBG_BRIGHT) + 4;
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)(d->uptime_s / 3600), (unsigned)(d->uptime_s / 60 % 60),
             (unsigned)(d->uptime_s % 60));
    x = put(fb, x, 5, buf, DBG_TEXT) + 4;
    put(fb, x, 5, d->activity == 0 ? "ACT" : d->activity == 1 ? "IDL" : "SLP", d->activity ? DBG_WARN : DBG_GOOD);
    snprintf(buf, sizeof(buf), "%uF %uMS", d->fps, d->frame_ms);
    put_right(fb, FX_W - top_inset, 5, buf, DBG_TEXT);

    /* a crash's address leaves no room for RST: CRSH says it */
    const bool crash_pc = d->crash == FX_CRASH_FAULT && d->crash_pc;
    x = crash_pc ? 7 : put(fb, 7, 12, "RST", DBG_LABEL) + 3;
    if (d->crash == FX_CRASH_HANG) {
        /* no picture came after power-up (1) or waking (2): it restarted itself */
        x = put(fb, x, 12, "HANG", DBG_BAD) + 3;
        snprintf(buf, sizeof(buf), "%u", (unsigned)d->crash_code);
        put(fb, x, 12, buf, DBG_LABEL);
    } else if (d->crash == FX_CRASH_FAULT) {
        x = put(fb, x, 12, crash_pc ? "CRSH" : "CRASH", DBG_BAD) + 3;
        snprintf(buf, sizeof(buf), "%02X", (unsigned)d->crash_code);
        x = put(fb, x, 12, buf, DBG_LABEL) + 3;
        if (crash_pc) {
            /* in flash, 5 hex digits; anywhere else (RAM) won't fit */
            if (d->crash_pc < 0x100000) {
                snprintf(buf, sizeof(buf), "%05X", (unsigned)d->crash_pc);
            } else {
                strcpy(buf, "RAM");
            }
            put(fb, x, 12, buf, DBG_TEXT);
        }
    } else {
        const bool fault = (d->reset_cause & (1 << 4 | 1 << 8 | 1 << 2)) != 0;
        x = put(fb, x, 12, reset_name(d->reset_cause), fault ? DBG_BAD : DBG_TEXT) + 3;
        snprintf(buf, sizeof(buf), "%02X", (unsigned)(d->reset_cause & 0xff));
        put(fb, x, 12, buf, DBG_LABEL);
    }
    if (d->heap_known) {
        x = put(fb, 73, 12, "MEM", DBG_LABEL) + 3;
        snprintf(buf, sizeof(buf), "%u%%", d->heap_used);
        x = put(fb, x, 12, buf, d->heap_used > 90 ? DBG_BAD : DBG_TEXT) + 3;
        snprintf(buf, sizeof(buf), "^%u", d->heap_peak);
        put(fb, x, 12, buf, d->heap_peak > 95 ? DBG_BAD : DBG_LABEL);
    }
    rule(fb, 21);

    /* the links */
    put_right(fb, DBG_COL_RSSI, 23, "DBM", DBG_LABEL);
    put_right(fb, DBG_COL_LEVEL, 23, "%", DBG_LABEL);
    put_right(fb, DBG_COL_MV, 23, "V", DBG_LABEL);
    put_right(fb, DBG_COL_DROPS, 23, "DR", DBG_LABEL);
    put_right(fb, DBG_COL_REASON, 23, "RS", DBG_LABEL);
    put_right(fb, DBG_COL_PRESSES, 23, "KEY", DBG_LABEL);
    put_right(fb, DBG_COL_CHARGE, 23, "CH", DBG_LABEL);
    char mv_buf[6];
    for (int half = 0; half < 2; half++) {
        bool estimate;
        const uint8_t level = half_level(in, half, &estimate);
        const bool charging = in->status && in->status->charging[half];
        if (level) {
            snprintf(buf, sizeof(buf), estimate ? " %u" : "%u", level); /* an estimate: a ~ in the space */
        } else {
            strcpy(buf, "--");
        }
        /* the half's volts, to the millivolt, when its firmware sends them (the charger's
         * while charging, so dim then) */
        const unsigned mv = d->half_mv[half] > 9999 ? 9999 : d->half_mv[half];
        if (mv) {
            snprintf(mv_buf, sizeof(mv_buf), "%u.%03u", mv / 1000, mv % 1000);
        } else {
            strcpy(mv_buf, "--");
        }
        debug_link(fb, 31 + half * 8, "LR"[half], &d->half[half], buf, level == 0 ? DBG_LABEL : battery_color(level),
                   mv_buf, mv && !charging ? DBG_TEXT : DBG_LABEL, d->presses[half], d->half_reads[half],
                   d->half_err[half], false);
        if (level && estimate) {
            put(fb, DBG_COL_LEVEL - 5 * (int)strlen(buf), 31 + half * 8, "~", DBG_WARN);
        }
        /* charging: a bolt just right of the level, flashing until 100, so
         * the level can be watched going up against the millivolts */
        charging_bolt(fb, DBG_COL_LEVEL + 1, 31 + half * 8, in->status, half);
    }
    const bool usb_out = in->status && in->status->usb;
    snprintf(buf, sizeof(buf), "BT%u", in->status ? in->status->profile : 0);
    /* the dongle's own supply in the V column: its 3.3 V regulator */
    if (d->dongle_mv) {
        const unsigned mv = d->dongle_mv > 9999 ? 9999 : d->dongle_mv;
        snprintf(mv_buf, sizeof(mv_buf), "%u.%03u", mv / 1000, mv % 1000);
    } else {
        snprintf(mv_buf, sizeof(mv_buf), "%s", d->dongle_mv_why[0] ? d->dongle_mv_why : "--");
    }
    debug_link(fb, 47, 'H', &d->host, usb_out ? "USB" : buf, usb_out ? DBG_LABEL : DBG_TEXT, mv_buf,
               d->dongle_mv ? DBG_TEXT : d->dongle_mv_why[0] ? DBG_WARN : DBG_LABEL, d->presses[0] + d->presses[1],
               NULL, 0, usb_out);
    rule(fb, 56);

    /* the profiles, then USB: its state, the reports sent over it and those
     * that failed (the output in use is in the H row) */
    profiles(fb, d, in->status, 6, 59);
    x = put(fb, 50, 59, "USB", DBG_LABEL) + 3;
    x = put(fb, x, 59, d->usb == 2 ? "HID" : d->usb == 1 ? "PWR" : "--",
            d->usb == 2 ? (usb_out && in->status->host_connected ? DBG_GOOD : DBG_TEXT) : DBG_TEXT) + 6;
    x = put(fb, x, 59, "RPT", DBG_LABEL);
    count_text(buf, sizeof(buf), d->usb_reports);
    x = put(fb, x, 59, buf, DBG_TEXT) + 4;
    x = put(fb, x, 59, "E", DBG_LABEL);
    snprintf(buf, sizeof(buf), "%u", d->usb_errors > 99 ? 99 : d->usb_errors);
    put(fb, x, 59, buf, d->usb_errors ? DBG_WARN : DBG_TEXT);

    /* layer (number, name, all that are on); the modifiers, lit while held
     * (either side); typing speed, right-aligned in room for three figures
     * so the modifiers stay put */
    snprintf(buf, sizeof(buf), "%uWPM", d->wpm > 999 ? 999 : d->wpm);
    put_right(fb, FX_W - 4, 68, buf, DBG_TEXT);
    static const char mod_glyph[4] = {FX_GLYPH_CTRL, FX_GLYPH_ALT, FX_GLYPH_SHIFT, FX_GLYPH_GUI};
    static const uint8_t mod_bit[4] = {0x11, 0x44, 0x22, 0x88}; /* HID bits, left and right */
    const int mods_x = FX_W - 4 - 5 * 6 - 6 - 4 * 7;
    for (int i = 0; i < 4; i++) {
        const bool on = (d->mods & mod_bit[i]) != 0;
        const int gx = mods_x + i * 7;
        if (on) {
            for (int dy = -1; dy <= 7; dy++) {
                line(fb, gx - 1, 68 + dy, gx + 5, 68 + dy, DBG_TEXT);
            }
        }
        const char g[2] = {mod_glyph[i], '\0'};
        put(fb, gx, 68, g, on ? DBG_BG : DBG_LABEL);
    }
    snprintf(buf, sizeof(buf), "L%u", d->layer);
    x = put(fb, 5, 68, buf, DBG_BRIGHT) + 3;
    /* the name gets what room the layer bits leave before the modifiers */
    const int name_room = (mods_x - 4 - 4 * 5 - 3 - x) / 5;
    if (d->layer_name && name_room > 0) {
        snprintf(buf, sizeof(buf), "%.*s", name_room > 7 ? 7 : name_room, d->layer_name);
        for (char *c = buf; *c; c++) {
            *c = *c >= 'a' && *c <= 'z' ? *c - 32 : *c; /* the small letters are for typed text */
        }
        x = put(fb, x, 68, buf, DBG_TEXT) + 3;
    }
    snprintf(buf, sizeof(buf), "%04X", (unsigned)(d->layers & 0xffff));
    put(fb, x, 68, buf, DBG_LABEL);

    /* each half's average draw in milliwatts; under the modifiers, the
     * dongle's chip temperature (amber from 60) */
    x = put(fb, 5, 76, "DRAW", DBG_LABEL) + 3;
    for (int half = 0; half < 2; half++) {
        const int hundredths = d->drain_hundredths_mw[half];
        const bool rough = hundredths >= 0 && d->drain_rough[half];
        x = put(fb, x, 76, half ? "R" : "L", DBG_LABEL) + 1;
        drain_text(buf, sizeof(buf), hundredths, rough);
        if (rough) {
            put(fb, x, 76, "~", DBG_WARN);
        }
        x = put(fb, x, 76, buf, drain_color(hundredths, rough, d->battery_mah)) + 3;
    }
    put(fb, x - 2, 76, "mW", DBG_LABEL);
    if (d->dongle_temp_known) {
        const int c = d->dongle_temp_c < -9 ? -9 : d->dongle_temp_c > 99 ? 99 : d->dongle_temp_c;
        snprintf(buf, sizeof(buf), "%dC", c);
        put_right(fb, FX_W - 4, 76, buf, c >= 60 ? DBG_WARN : DBG_TEXT);
    } else {
        snprintf(buf, sizeof(buf), "%sC", d->dongle_temp_why[0] ? d->dongle_temp_why : "--");
        put_right(fb, FX_W - 4, 76, buf, d->dongle_temp_why[0] ? DBG_WARN : DBG_LABEL);
    }
    rule(fb, 85);

    /* the last raw events, the newest at the bottom and brightest */
    for (int i = 0; i < d->n_log && i < FX_DEBUG_LOG; i++) {
        event_text(buf, sizeof(buf), &d->log[i]);
        put(fb, 5, 87 + i * 8, buf, i == d->n_log - 1 ? DBG_BRIGHT : DBG_TEXT);
    }

    /* the quickest a key has come back down after going up: a switch that
     * bounces shows here as a few milliseconds. This line is in the bottom
     * corners' curve, so it keeps in from both sides: up out of it as far as the log leaves room */
    const int bottom_inset = fx_corner_inset(110, 116);
    x = put(fb, bottom_inset, 110, "BNC", DBG_LABEL) + 3;
    if (d->bounce_ms) {
        snprintf(buf, sizeof(buf), "%uMS P%02u", d->bounce_ms, d->bounce_key);
        x = put(fb, x, 110, buf, d->bounce_ms < 30 ? DBG_BAD : DBG_TEXT);
    } else {
        x = put(fb, x, 110, "--", DBG_LABEL);
    }

    /* The board and the keyboard's name under it start in the same column:
     * just right of the events (15 letters at most) and the bounce reading,
     * or as far left as the bottom right corner's curve lets the board be.
     * A name too long for the room moves left, then loses words from the
     * front (AURORA CORNE: CORNE), and then letters from the end. */
    const int right = FX_W - bottom_inset;
    const int board_pitch = board_cols > 10 ? 4 : 5;
    const int board_max = FX_W - 1 - fx_corner_inset(88, 108) - board_cols * board_pitch;
    int column = 5 + 15 * 5 + 3;
    column = column < x + 5 ? x + 5 : column;
    const int board_left = column < board_max ? column : board_max;
    board(fb, in, board_left, 88, board_pitch, 4, 1, 1);
    if (d->keyboard) {
        /* a name that doesn't fit there moves left first, as far as the bounce lets it */
        const int least = x + 5;
        const int room = (right - least + 2) / 5; /* letters, 5 apart, the last 4 wide */
        const char *name = d->keyboard;
        while ((int)strlen(name) > room && strchr(name, ' ') != NULL) {
            name = strchr(name, ' ') + 1;
        }
        snprintf(buf, sizeof(buf), "%.*s", room < 0 ? 0 : room > (int)sizeof(buf) - 1 ? (int)sizeof(buf) - 1 : room,
                 name);
        for (char *c = buf; *c; c++) {
            *c = *c >= 'a' && *c <= 'z' ? *c - 32 : *c;
        }
        const int wide = 5 * (int)strlen(buf) - 1;
        int left = board_left > least ? board_left : least;
        left = left + wide > right ? right - wide : left;
        put(fb, left < least ? least : left, 110, buf, DBG_LABEL);
    }
}

/* --- keys: the keyboard, the three links and how busy it all is ------------- */

/*
 * The Debug screen's map of the keys with room to see it, and the readings
 * that are worth watching move. Top to bottom:
 *
 *   three blocks: the left half, the computer and the right half. Each has
 *   its battery and level (the computer: the Bluetooth profiles as the
 *   Debug screen draws them), its signal as bars and in dBm, and that
 *   signal over the last SIGNAL_W seconds;
 *
 *   the board, colours as `board` has them;
 *
 *   a level meter under each column of the board, as on a hi-fi: a key
 *   pressed in the column sends its bar up (and its neighbours' a little),
 *   more keys send it higher, and it falls back in about two seconds. A mark
 *   stays at the highest each bar got to for a moment, then falls after it.
 *
 * Over USB the middle block has no radio link to show: it has the typing
 * speed where the signal would be, and that speed over the same seconds
 * (SPEED_FULL words a minute fill it, or the fastest in it if that is more).
 *
 * It takes the whole screen, so the status text isn't drawn over it.
 */

#define SIGNAL_W 40
#define SIGNAL_H 9
#define SPEED_FULL 60 /* words a minute */
#define KEYS_BOARD_TOP 39
#define KEYS_BOARD_H 50
#define METER_COLS 16
#define METER_STEPS 6     /* lights in a bar */
#define METER_STEP_H 2    /* each this high, a row apart */
#define METER_FOOT 108 /* high enough that the outer bars clear the bottom corners' curve */
#define METER_KEY 0.55f   /* what a key adds to its column's bar, of a full one */
#define METER_SPILL 0.2f  /* and to the bars beside it */
#define METER_FALL 0.5f   /* full bars a second */
#define METER_HOLD 1.2f   /* seconds the mark stays up */
#define METER_MARK_FALL 0.3f

static float meter_level[METER_COLS], meter_mark[METER_COLS], meter_held[METER_COLS];
static float keys_time; /* 30ths of a second */

static void keys_start(uint16_t *palette) {
    debug_start(palette);
    memset(meter_level, 0, sizeof(meter_level));
    memset(meter_mark, 0, sizeof(meter_mark));
    memset(meter_held, 0, sizeof(meter_held));
    keys_time = 0;
}

/* A row of values as columns `high` at most (15 or under), on a line: each
 * a dim column topped in `color`, or a solid bar in amber or red if the
 * value has SPARK_WARN or SPARK_RED added, so the bad seconds stand out. */
#define SPARK_HIGH 0x0f
#define SPARK_WARN 0x10
#define SPARK_RED 0x20

static void spark(uint8_t *fb, int x, int y, const uint8_t *values, int count, int high, uint8_t color) {
    for (int i = 0; i < count; i++) {
        const int v = (values[i] & SPARK_HIGH) > high ? high : values[i] & SPARK_HIGH;
        const uint8_t top = values[i] & SPARK_RED ? DBG_BAD : values[i] & SPARK_WARN ? DBG_WARN : color;
        const bool trouble = values[i] & (SPARK_RED | SPARK_WARN);
        plot(fb, x + i, y + high - 1, DBG_DIM);
        for (int up = 0; up < v; up++) {
            plot(fb, x + i, y + high - 1 - up, trouble || up == v - 1 ? top : DBG_DIM);
        }
    }
}

static int keys_block_x(int place) {
    return (FX_W - SIGNAL_W) / 2 + (place - 1) * (SIGNAL_W + 2);
}

/* A battery, 10 by 6 with its cap on the right: as full as `level` percent,
 * an empty outline when that isn't known (0). */
static void battery_icon(uint8_t *fb, int x, int y, int level, uint8_t color) {
    line(fb, x, y, x + 8, y, color);
    line(fb, x, y + 5, x + 8, y + 5, color);
    line(fb, x, y, x, y + 5, color);
    line(fb, x + 8, y, x + 8, y + 5, color);
    line(fb, x + 9, y + 2, x + 9, y + 3, color);
    const int full = level <= 0 ? 0 : level >= 100 ? 5 : 1 + level * 4 / 100;
    for (int i = 0; i < full; i++) {
        line(fb, x + 2 + i, y + 2, x + 2 + i, y + 3, color);
    }
}

/*
 * Where things go across a block of the Keys screen, from its left edge, so
 * that nothing moves as readings change: a charging bolt, the battery with
 * the signal bars under it, then the level with the dBm under it, both
 * left-aligned with room for four characters ("100%", "-100").
 */
#define KEYS_ICON_X 7  /* the battery, and the bars: after the bolt (5) and a gap */
#define KEYS_TEXT_X 20 /* the level, and the dBm: after the battery (10) and a gap */

/* One of the three blocks' signal: the bars, the dBm at `top` and under
 * them the last SIGNAL_W seconds of `log` (fx_debug's signal_log). `place`
 * 0 on the left, 1 in the middle, 2 on the right. */
static void keys_link(uint8_t *fb, int place, int top, const struct fx_link *link, const int8_t *log) {
    char dbm[8] = "";
    uint8_t dbm_color = DBG_LABEL;
    if (!link->up) {
        strcpy(dbm, "--");
        dbm_color = DBG_BAD;
    } else if (link->rssi_known) {
        snprintf(dbm, sizeof(dbm), "%d", link->rssi);
        dbm_color = signal_color(link);
    }
    const int left = keys_block_x(place);
    signal_bars(fb, left + KEYS_ICON_X, top, link);
    put(fb, left + KEYS_TEXT_X, top, dbm, dbm_color);
    /* -95 dBm and under is one row, -40 and over all of them; a second of
     * weak signal keeps its colour in the history */
    uint8_t high[SIGNAL_W];
    for (int i = 0; i < SIGNAL_W; i++) {
        const int dbm = log[FX_HISTORY - SIGNAL_W + i];
        high[i] = 0;
        if (dbm != 0 && dbm != FX_SIGNAL_DOWN) {
            const int h = 1 + (dbm + 95) * (SIGNAL_H - 1) / 55;
            high[i] = (uint8_t)(h < 1 ? 1 : h > SIGNAL_H ? SIGNAL_H : h);
            high[i] |= dbm > SIGNAL_GOOD_DBM ? 0 : dbm > SIGNAL_WARN_DBM ? SPARK_WARN : SPARK_RED;
        }
    }
    spark(fb, keys_block_x(place), top + 9, high, SIGNAL_W, SIGNAL_H, DBG_TEXT);
}

/* The typing speed over the last SIGNAL_W seconds, in the middle block from
 * `y`: SPEED_FULL words a minute fill it, or the fastest in it if more. */
static void speed_spark(uint8_t *fb, const struct fx_debug *d, int y) {
    const uint8_t *speed = d->wpm_log + FX_HISTORY - SIGNAL_W;
    int full = SPEED_FULL;
    for (int i = 0; i < SIGNAL_W; i++) {
        full = speed[i] > full ? speed[i] : full;
    }
    uint8_t high[SIGNAL_W];
    for (int i = 0; i < SIGNAL_W; i++) {
        high[i] = (uint8_t)((speed[i] * SIGNAL_H + full - 1) / full);
    }
    spark(fb, keys_block_x(1), y, high, SIGNAL_W, SIGNAL_H, DBG_TEXT);
}

static void keys_frame(uint8_t *fb, const struct fx_input *in) {
    memset(fb, DBG_BG, PIXELS);
    if (in->debug == NULL) {
        return;
    }
    const struct fx_debug *d = in->debug;
    char buf[32];

    keys_time += in->step;

    /* the halves: a battery and its level, in their places in the block */
    for (int half = 0; half < 2; half++) {
        const bool charging = in->status && in->status->charging[half];
        /* while charging: a bolt to the left of the battery, flashing until
         * 100, and the level, estimated (with a ~) as ZMK's reads high */
        bool estimate;
        const uint8_t level = half_level(in, half, &estimate);
        const uint8_t color = level == 0                 ? DBG_LABEL
                              : level <= FX_LOW_BATTERY  ? DBG_BAD
                              : level <= FX_WARN_BATTERY ? DBG_WARN
                                                         : DBG_TEXT;
        if (level == 0) {
            strcpy(buf, "--");
        } else {
            snprintf(buf, sizeof(buf), estimate ? " %u%%" : "%u%%", level); /* a ~ in the space */
        }
        const int left = keys_block_x(half * 2);
        /* a low battery flashes, unless it is charging (its bolt does) */
        if (charging || !fx_battery_flash_off(level, (uint32_t)(keys_time * 1000 / 30))) {
            battery_icon(fb, left + KEYS_ICON_X, 6, level, color);
            put(fb, left + KEYS_TEXT_X, 6, buf, color);
            if (level && estimate) {
                put(fb, left + KEYS_TEXT_X, 6, "~", DBG_WARN);
            }
        }
        charging_bolt(fb, left, 6, in->status, half);
        keys_link(fb, half * 2, 15, &d->half[half], d->signal_log[half]);
    }
    /* the computer */
    if (in->status && in->status->usb) {
        /* no radio in it: the port (green when the computer is listening,
         * amber when it has put the port to sleep), and the typing speed
         * where the halves have their signal */
        put(fb, (FX_W - 15 + 1) / 2, 6, "USB",
            d->usb_asleep ? DBG_WARN : in->status->host_connected ? DBG_GOOD : DBG_BAD);
        /* the speed right-aligned in room for three figures, so "WPM" stays put */
        snprintf(buf, sizeof(buf), "%u", d->wpm > 999 ? 999 : d->wpm);
        const int wpm_x = (FX_W - (15 + 1 + 15)) / 2 + 15 + 1;
        put_right(fb, wpm_x - 1, 15, buf, d->wpm ? DBG_BRIGHT : DBG_TEXT);
        put(fb, wpm_x, 15, "WPM", DBG_LABEL);
        speed_spark(fb, d, 24);
    } else {
        /* the Bluetooth profiles as the Debug screen draws them, and the link */
        profiles(fb, d, in->status, (FX_W - (FX_DEBUG_PROFILES * 8 - 3)) / 2, 6);
        keys_link(fb, 1, 15, &d->host, d->signal_log[2]);
    }
    rule(fb, 35);

    /* the board: as big as fits across, and no taller than its space */
    int rows = 1;
    for (int key = 0; key < key_count; key++) {
        const int row = (board_row(key) < 4 ? board_row(key) : 4) + 1;
        rows = row > rows ? row : rows;
    }
    /* in from the sides by as much as the meters' bottom lights need, at least 3 */
    const int meter_top = METER_FOOT - (METER_STEPS - 1) * (METER_STEP_H + 1) - (METER_STEP_H - 1);
    const int margin = fx_corner_inset(meter_top, METER_FOOT) > 3 ? fx_corner_inset(meter_top, METER_FOOT) : 3;
    const int split = 4, gap = 2, pitch = (FX_W - 2 * margin - split) / board_cols;
    int high = pitch - gap;
    if (rows * (high + gap) - gap > KEYS_BOARD_H) {
        high = (KEYS_BOARD_H + gap) / rows - gap;
    }
    const int left = (FX_W - (board_cols * pitch - gap + split)) / 2;
    board(fb, in, left, KEYS_BOARD_TOP + (KEYS_BOARD_H - (rows * (high + gap) - gap)) / 2, pitch, high, gap, split);

    /* the meters: up with the keys of this frame, then down with the time */
    const int cols = board_cols < METER_COLS ? board_cols : METER_COLS;
    const float seconds = in->step / 30;
    for (int k = 0; k < in->n_keys; k++) {
        const int col = in->keys[k] < key_count ? board_col(in->keys[k]) : -1;
        for (int near = col - 1; col >= 0 && near <= col + 1; near++) {
            if (near >= 0 && near < cols) {
                meter_level[near] = clampf(meter_level[near] + (near == col ? METER_KEY : METER_SPILL), 0, 1);
            }
        }
    }
    for (int col = 0; col < cols; col++) {
        if (meter_level[col] >= meter_mark[col]) {
            meter_mark[col] = meter_level[col];
            meter_held[col] = METER_HOLD;
        } else if (meter_held[col] > 0) {
            meter_held[col] -= seconds;
        } else {
            meter_mark[col] = clampf(meter_mark[col] - METER_MARK_FALL * seconds, 0, 1);
        }
        const int lit = (int)(meter_level[col] * METER_STEPS + 0.5f), mark = (int)(meter_mark[col] * METER_STEPS + 0.5f);
        const int x = left + col * pitch + (col >= board_cols / 2 ? split : 0);
        for (int step = 0; step < METER_STEPS; step++) {
            /* the top light red and the one under it amber, as a hi-fi's are */
            const uint8_t color = step < lit ? (step == METER_STEPS - 1   ? DBG_BAD
                                                : step == METER_STEPS - 2 ? DBG_WARN
                                                                          : DBG_HEAT + 6 + step * 3)
                                  : step == mark - 1 ? DBG_BRIGHT
                                  : step == 0        ? DBG_DIM
                                                     : DBG_BG;
            const int y = METER_FOOT - step * (METER_STEP_H + 1);
            for (int row = 0; row < METER_STEP_H && color != DBG_BG; row++) {
                line(fb, x, y - row, x + pitch - gap - 1, y - row, color);
            }
        }
        meter_level[col] = clampf(meter_level[col] - METER_FALL * seconds, 0, 1);
    }
}

/* --- status: halfway between Keys and Debug ---------------------------------- */

/*
 * The readings worth glancing at, set out like a racing car's steering
 * wheel display: lights across the top, the big figure in the middle, bars
 * and graphs rather than columns of numbers. Top to bottom:
 *
 *   the typing speed as a row of lights, green then red then violet, with a
 *   mark at the fastest of the last FX_HISTORY seconds;
 *
 *   each half of the board at its side, the speed big between them;
 *
 *   each link as on Keys, the halves' over their batteries and the
 *   computer's between: the bars and dBm, under them the last SIGNAL_W
 *   seconds (over USB, the middle has USB and the typing speed instead);
 *   and under that a strip of the last FX_CONN_HISTORY slots of
 *   FX_CONN_SLOT_S seconds: green, amber or red by the weakest signal in
 *   it, red where the link was down, a dim line where the keyboard was
 *   idle (so the halves may have been asleep);
 *
 *   each half's battery at its side: its level, big, and beside it its
 *   volts and average draw, their units between the halves; under them a
 *   bar;
 *
 *   along the foot, a row of boxes: the output, the drops, the dongle's
 *   supply and temperature, and the worst of what has gone wrong (filled
 *   red or amber).
 *
 * It takes the whole screen, so the status text isn't drawn over it.
 */

#define STATUS_LIGHTS 15
#define STATUS_LIGHT_WPM 8 /* each light: the 15th is lit at 120 */
#define STATUS_LIGHTS_Y 3
#define STATUS_KEYS_TOP 11 /* the halves, the speed big between them */
#define STATUS_KEYS_H 35
#define STATUS_SIGNAL_Y 50 /* each link's bars and dBm, its graph under them */
#define STATUS_CONN_Y 70   /* and under that its connection strip, this high: */
#define STATUS_CONN_H 3
#define STATUS_BATTERY_Y 79
#define STATUS_BOX_Y 107 /* along the foot */
#define STATUS_BATTERY_W 61
#define STATUS_MID_W 57 /* from a half's outer side to its readings' inner end */

static void status_start(uint16_t *palette) { debug_start(palette); }

/* Text `scale` times the size. */
static void put_big(uint8_t *fb, int x, int y, const char *s, uint8_t color, int scale) {
    for (; *s; s++, x += 5 * scale) {
        const uint8_t *rows = fx_glyph_rows(*s);
        for (int row = 0; rows && row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (rows[row] & (0x10 >> col)) {
                    for (int dy = 0; dy < scale; dy++) {
                        line(fb, x + col * scale, y + row * scale + dy, x + col * scale + scale - 1,
                             y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static void fill(uint8_t *fb, int x, int y, int wide, int high, uint8_t color) {
    for (int dy = 0; dy < high; dy++) {
        line(fb, x, y + dy, x + wide - 1, y + dy, color);
    }
}

/* A box `wide` across at (x, y), 9 high, with `text` in the middle: an
 * outline when `fill_color` is DBG_BG, else filled and the text dark. */
static void status_box(uint8_t *fb, int x, int y, int wide, const char *text, uint8_t color, uint8_t fill_color) {
    if (fill_color != DBG_BG) {
        fill(fb, x, y, wide, 9, fill_color);
        color = DBG_BG;
    } else {
        line(fb, x, y, x + wide - 1, y, DBG_DIM);
        line(fb, x, y + 8, x + wide - 1, y + 8, DBG_DIM);
        line(fb, x, y, x, y + 8, DBG_DIM);
        line(fb, x + wide - 1, y, x + wide - 1, y + 8, DBG_DIM);
    }
    put(fb, x + (wide - (5 * (int)strlen(text) - 1)) / 2, y + 1, text, color);
}

/* Text against `edge` from the middle's side: the left half's ends at it,
 * the right half's starts at it. */
static void status_text(uint8_t *fb, int half, int edge, int y, const char *s, uint8_t color) {
    if (half) {
        put(fb, edge, y, s, color);
    } else {
        put_right(fb, edge, y, s, color);
    }
}

/* A half's battery, STATUS_BATTERY_W across from (x, y): its level, big
 * at its own side (while charging, estimated: with a ~ over the %), a bar
 * with the bolt at its inner end, and its latest volts and draw. */
static void status_half(uint8_t *fb, const struct fx_input *in, int half, int x, int y) {
    const struct fx_debug *d = in->debug;
    const int right = x + STATUS_BATTERY_W;
    char buf[20];
    bool estimate;
    const uint8_t level = half_level(in, half, &estimate);
    const bool charging = in->status && in->status->charging[half];
    /* the figures big, the % small at their foot, to leave room beside it */
    if (level) {
        snprintf(buf, sizeof(buf), "%u", level);
    } else {
        strcpy(buf, "--");
    }
    const int level_w = 10 * (int)strlen(buf) - 2 + (level ? 6 : 0), level_x = half ? right - level_w : x;
    put_big(fb, level_x, y, buf, level ? battery_color(level) : DBG_LABEL, 2);
    if (level) {
        put(fb, level_x + level_w - 4, y + 7, "%", battery_color(level));
        if (estimate) {
            put(fb, level_x + level_w - 4, y, "~", DBG_WARN);
        }
    }
    const int lit = !level ? 0 : level < 15 ? 1 : (level + 5) / 10;
    for (int step = 0; step < 10; step++) {
        const bool on = step < lit;
        fill(fb, (half ? x + 2 : x) + step * 6, y + 18, 5, 3,
             !on ? DBG_DIM : level <= FX_WARN_BATTERY ? battery_color(level) : DBG_GOOD);
    }
    charging_bolt(fb, half ? x - 4 : right, y + 16, in->status, half); /* at the bar's inner end, under the mW */

    /* beside the level its latest volts (the charger's while charging, so
     * dim then) and draw, aligned to the middle, where their units are;
     * the volts, wider, reach further in */
    const int mid = half ? x + STATUS_BATTERY_W - STATUS_MID_W : x + STATUS_MID_W; /* left: right edge */
    const int v_mid = half ? mid - 3 : mid + 4; /* the same gap each side of the V */
    const unsigned mv = d->half_mv[half] > 9999 ? 9999 : d->half_mv[half];
    if (mv) {
        snprintf(buf, sizeof(buf), "%u.%03u", mv / 1000, mv % 1000);
    } else {
        strcpy(buf, "--");
    }
    status_text(fb, half, v_mid, y, buf, mv && !charging ? DBG_TEXT : DBG_LABEL);
    const int hundredths = d->drain_hundredths_mw[half];
    const bool rough = hundredths >= 0 && d->drain_rough[half];
    drain_text(buf, sizeof(buf), hundredths, rough);
    if (rough) {
        put(fb, half ? mid : mid - 5 * (int)strlen(buf), y + 8, "~", DBG_WARN); /* in the space before it */
    }
    status_text(fb, half, mid, y + 8, buf, drain_color(hundredths, rough, d->battery_mah));
}

static void status_frame(uint8_t *fb, const struct fx_input *in) {
    memset(fb, DBG_BG, PIXELS);
    if (in->debug == NULL) {
        return;
    }
    const struct fx_debug *d = in->debug;
    const bool usb_out = in->status && in->status->usb;
    char buf[16];

    /* the lights: the speed, and a mark at the fastest lately */
    int fastest = d->wpm;
    for (int i = 0; i < FX_HISTORY; i++) {
        fastest = d->wpm_log[i] > fastest ? d->wpm_log[i] : fastest;
    }
    const int lights_x = (FX_W - (STATUS_LIGHTS * 8 - 2)) / 2; /* clear of the rounded corners */
    for (int i = 0; i < STATUS_LIGHTS; i++) {
        const int at = (i + 1) * STATUS_LIGHT_WPM;
        const uint8_t on = i < 5 ? DBG_GOOD : i < 10 ? DBG_BAD : DBG_LAYER;
        const uint8_t color = d->wpm >= at ? on
                              : fastest >= at && fastest < at + STATUS_LIGHT_WPM ? DBG_BRIGHT
                                                                                 : DBG_DIM;
        fill(fb, lights_x + i * 8, STATUS_LIGHTS_Y, 6, 4, color);
    }

    /* the halves at the sides, the speed big between them */
    int rows = 1;
    for (int key = 0; key < key_count; key++) {
        const int row = (board_row(key) < 4 ? board_row(key) : 4) + 1;
        rows = row > rows ? row : rows;
    }
    const int gap = 1, pitch = board_cols > 10 ? 7 : 9;
    const int high = (STATUS_KEYS_H + gap) / rows - gap;
    board(fb, in, 4, STATUS_KEYS_TOP, pitch, high > pitch - gap ? pitch - gap : high, gap,
          FX_W - 8 - (board_cols * pitch - gap));
    snprintf(buf, sizeof(buf), "%u", d->wpm > 999 ? 999 : d->wpm);
    /* the speed and its label under it, together in the middle of the halves */
    const int wpm_y = STATUS_KEYS_TOP + (STATUS_KEYS_H - 21 - 10) / 2;
    put(fb, (FX_W - 14) / 2, wpm_y + 26, "WPM", DBG_LABEL);
    put_big(fb, (FX_W - (15 * (int)strlen(buf) - 3)) / 2, wpm_y, buf,
            d->wpm ? DBG_BRIGHT : DBG_LABEL, 3);

    /* the boxes: the output, the drops, the dongle's supply and temperature,
     * then the worst trouble there is, if any; in from the sides, clear of
     * the panel's rounded corners */
    int x = 6;
    if (usb_out) {
        status_box(fb, x, STATUS_BOX_Y, 19, "USB", in->status->host_connected ? DBG_GOOD : DBG_WARN, DBG_BG);
    } else {
        snprintf(buf, sizeof(buf), "BT%u", in->status ? in->status->profile : 0);
        status_box(fb, x, STATUS_BOX_Y, 19, buf, d->host.up ? DBG_GOOD : DBG_TEXT, DBG_BG);
    }
    x += 21;
    const int drops = d->half[0].drops + d->half[1].drops + d->host.drops;
    snprintf(buf, sizeof(buf), "DR%d", drops > 99 ? 99 : drops);
    status_box(fb, x, STATUS_BOX_Y, 24, buf, DBG_LABEL, drops ? DBG_WARN : DBG_BG);
    x += 26;
    if (d->dongle_mv) {
        const unsigned mv = d->dongle_mv > 9999 ? 9999 : d->dongle_mv;
        snprintf(buf, sizeof(buf), "%u.%03uV", mv / 1000, mv % 1000);
    } else {
        snprintf(buf, sizeof(buf), "%sV", d->dongle_mv_why[0] ? d->dongle_mv_why : "--");
    }
    status_box(fb, x, STATUS_BOX_Y, 34, buf, d->dongle_mv ? DBG_TEXT : DBG_LABEL, DBG_BG);
    x += 36;
    if (d->dongle_temp_known) {
        const int c = d->dongle_temp_c < -9 ? -9 : d->dongle_temp_c > 99 ? 99 : d->dongle_temp_c;
        snprintf(buf, sizeof(buf), "%dC", c);
        status_box(fb, x, STATUS_BOX_Y, 19, buf, DBG_TEXT, c >= 60 ? DBG_WARN : DBG_BG);
    } else {
        status_box(fb, x, STATUS_BOX_Y, 19, "--C", DBG_LABEL, DBG_BG);
    }
    x += 21;
    /* the worst: a restart that hung or crashed, a bouncing switch, a half's
     * charging state that failed, USB reports the computer didn't take */
    const char *trouble = NULL;
    uint8_t trouble_color = DBG_BAD;
    if (d->crash == FX_CRASH_HANG) {
        trouble = "HANG";
    } else if (d->crash == FX_CRASH_FAULT) {
        trouble = "CRSH";
    } else if (d->bounce_ms && d->bounce_ms < 30) {
        snprintf(buf, sizeof(buf), "B%u", d->bounce_ms);
        trouble = buf;
    } else if (d->half_reads[0][0] == 'E' || d->half_reads[0][0] == 'T' || d->half_reads[1][0] == 'E' ||
               d->half_reads[1][0] == 'T') {
        trouble = "CH";
    } else if (d->usb_errors) {
        snprintf(buf, sizeof(buf), "UE%u", d->usb_errors > 9 ? 9 : d->usb_errors);
        trouble = buf;
        trouble_color = DBG_WARN;
    }
    status_box(fb, x, STATUS_BOX_Y, FX_W - 6 - x, trouble ? trouble : "OK", trouble ? DBG_BG : DBG_GOOD,
               trouble ? trouble_color : DBG_BG);

    /* each link as on Keys, and under it its connection strip */
    if (usb_out) {
        put(fb, (FX_W - 15 + 1) / 2, STATUS_SIGNAL_Y, "USB",
            d->usb_asleep ? DBG_WARN : in->status->host_connected ? DBG_GOOD : DBG_BAD);
        speed_spark(fb, d, STATUS_SIGNAL_Y + 9);
    } else {
        keys_link(fb, 1, STATUS_SIGNAL_Y, &d->host, d->signal_log[2]);
    }
    keys_link(fb, 0, STATUS_SIGNAL_Y, &d->half[0], d->signal_log[0]);
    keys_link(fb, 2, STATUS_SIGNAL_Y, &d->half[1], d->signal_log[1]);
    static const int place[3] = {0, 2, 1};
    for (int link = 0; link < 3; link++) {
        const int x = keys_block_x(place[link]);
        for (int px = 0; px < FX_CONN_HISTORY && px < SIGNAL_W; px++) {
            const int v = d->conn_log[link][FX_CONN_HISTORY - SIGNAL_W + px];
            if (v == FX_CONN_IDLE) {
                plot(fb, x + px, STATUS_CONN_Y + STATUS_CONN_H - 1, DBG_DIM);
            } else if (v) {
                const uint8_t color = v == FX_SIGNAL_DOWN ? DBG_BAD
                                      : v == FX_CONN_USB || v > SIGNAL_GOOD_DBM ? DBG_GOOD
                                      : v > SIGNAL_WARN_DBM ? DBG_WARN
                                                            : DBG_BAD;
                line(fb, x + px, STATUS_CONN_Y, x + px, STATUS_CONN_Y + STATUS_CONN_H - 1, color);
            }
        }
    }

    /* each half's battery */
    status_half(fb, in, 0, 4, STATUS_BATTERY_Y);
    status_half(fb, in, 1, FX_W - 4 - STATUS_BATTERY_W, STATUS_BATTERY_Y);
    put(fb, FX_W / 2 - 2, STATUS_BATTERY_Y, "V", DBG_LABEL);
    put(fb, FX_W / 2 - 4, STATUS_BATTERY_Y + 8, "mW", DBG_LABEL);
}

/* --- the list --------------------------------------------------------------- */

/* How far down the status text sits: in the bottom corners, clear of their
 * rounding, as on Tracker, where it is under the line at TRACK_CY. Keys and
 * Debug draw their own. */
#define CORNER_DROP 12

static const struct {
    const char *name;
    void (*start)(uint16_t *palette);
    void (*frame)(uint8_t *fb, const struct fx_input *in);
    /* its colours: a function that fills in the theme's, or else its own for
     * the status text over it (colors.h), the picture's being in `start` */
    void (*colors)(uint16_t *palette);
    const struct text_colors *text;
    struct fx_theme theme; /* how that text is set out, see fx.h: fx_theme adds the colours */
    bool debug;            /* see fx_is_debug */
    bool keys;             /* see fx_shows_keys */
    uint8_t saver;         /* can be the screensaver: the way into it (FX_TR_*) */
} effects[] = {
    {"DEBUG", debug_start, debug_frame, debug_colors, NULL, {0}, .debug = true},
    {"STATUS", status_start, status_frame, debug_colors, NULL, {0}, .keys = true},
    {"KEYS", keys_start, keys_frame, debug_colors, NULL, {0}, .keys = true},
    {"THERMAL", thermal_start, thermal_frame, NULL, &thermal_text, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"TRACKER", tracker_start, tracker_frame, tracker_colors, NULL, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"TERMINAL", term_start, term_frame, term_colors, NULL, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"WARP", warp_start, warp_frame, NULL, &warp_text, {.dots = true, .drop = CORNER_DROP}, .saver = FX_TR_HYPERSPACE},
    {"CODE", code_start, code_frame, NULL, &code_text, {.font = FX_FONT_PIXEL, .dots = true, .drop = CORNER_DROP}, .saver = FX_TR_MELT},
    {"LIFE", life_start, life_frame, NULL, &life_text, {.dots = true, .drop = CORNER_DROP}, .saver = FX_TR_INFECT},
};
#define FX_COUNT ((int)(sizeof(effects) / sizeof(effects[0])))

int fx_count(void) { return FX_COUNT; }

const char *fx_name(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].name : ""; }

bool fx_is_debug(int fx) { return fx >= 0 && fx < FX_COUNT && effects[fx].debug; }

bool fx_shows_keys(int fx) { return fx >= 0 && fx < FX_COUNT && effects[fx].keys; }

/* The status text over an animation: set out as the table has it, in the
 * animation's own colours or the theme's. */
const struct fx_theme *fx_theme(int fx) {
    static struct fx_theme themed;
    const int which = fx >= 0 && fx < FX_COUNT ? fx : 0;
    const struct text_colors *own = effects[which].text;

    themed = effects[which].theme;
    themed.ink = color565(own ? own->ink : scheme->text);
    themed.dim = color565(own ? own->dim : scheme->label);
    themed.accent = color565(own ? own->accent : tracker_top());
    themed.search = color565(own ? own->search : tracker_text());
    themed.low = color565(own ? own->low : scheme->bad);
    /* around it: the theme's background on those in its colours, else black */
    themed.halo = own ? 0 : color565(scheme->bg);
    return &themed;
}

/* The fixed colours, and the theme's if the animation is in them. */
static void fill_colors(int fx, uint16_t *palette) {
    if (effects[fx].colors != NULL) {
        effects[fx].colors(palette);
    }
    palette[FX_BLACK] = color565(fixed_black);
    palette[FX_RED] = color565(fixed_red);
    palette[FX_GREY] = color565(fixed_grey);
    palette[FX_WHITE] = color565(fixed_white);
}

bool fx_recolor(int fx, uint16_t *palette) {
    if (fx < 0 || fx >= FX_COUNT || effects[fx].colors == NULL) {
        return false;
    }
    fill_colors(fx, palette);
    return true;
}

void fx_start(int fx, uint16_t *palette) {
    if (fx < 0 || fx >= FX_COUNT) {
        return;
    }
    memset(canvas, 0, PIXELS);
    memset(palette, 0, 256 * sizeof(palette[0]));
    effects[fx].start(palette);
    fill_colors(fx, palette);
}

void fx_frame(int fx, uint8_t *fb, const struct fx_input *in) {
    if (fx >= 0 && fx < FX_COUNT) {
        effects[fx].frame(fb, in);
    }
}

/* --- a notice over the picture ------------------------------------------------ */

/* Letters twice the size, as the Tracker's range is. */
static void big_text(uint8_t *fb, int x, int y, const char *s, uint8_t color) {
    for (; *s; s++, x += 10) {
        const uint8_t *rows = fx_glyph_rows(*s);
        for (int dot = 0; rows != NULL && dot < 35; dot++) {
            if (rows[dot / 5] & (0x10 >> dot % 5)) {
                for (int d = 0; d < 4; d++) {
                    plot(fb, x + dot % 5 * 2 + (d & 1), y + dot / 5 * 2 + (d >> 1), color);
                }
            }
        }
    }
}

void fx_draw_notice(uint8_t *fb, const char *title, const char *detail) {
    const int title_w = 10 * (int)strlen(title) - 2, detail_w = 5 * (int)strlen(detail) - 1;
    const int w = (title_w > detail_w ? title_w : detail_w) + 12, h = *detail ? 35 : 24;
    const int left = (FX_W - w) / 2, top = (FX_H - h) / 2;

    for (int y = top; y < top + h; y++) {
        for (int x = left; x < left + w; x++) {
            const bool edge = y == top || y == top + h - 1 || x == left || x == left + w - 1;
            plot(fb, x, y, edge ? FX_GREY : FX_BLACK);
        }
    }
    big_text(fb, (FX_W - title_w) / 2, top + 5, title, FX_WHITE);
    for (int x = (FX_W - detail_w) / 2; *detail; detail++, x += 5) {
        glyph(fb, x, top + 23, *detail, FX_GREY);
    }
}

/* --- the screensaver's transitions: see fx.h ---------------------------------- */

int fx_saver_count(void) {
    int count = 0;
    for (int fx = 0; fx < FX_COUNT; fx++) {
        count += effects[fx].saver != FX_TR_NONE;
    }
    return count;
}

int fx_saver(int n) {
    for (int fx = 0; fx < FX_COUNT; fx++) {
        if (effects[fx].saver != FX_TR_NONE && n-- == 0) {
            return fx;
        }
    }
    return -1;
}

int fx_saver_way_in(int fx) { return fx >= 0 && fx < FX_COUNT ? effects[fx].saver : FX_TR_NONE; }

/* How bright an RGB565 colour is, 0..255. */
static int brightness(uint16_t c) {
    return (((c >> 8) & 0xf8) * 2 + ((c >> 3) & 0xfc) * 5 + ((c << 3) & 0xf8)) / 8;
}

/* A colour dimmed to k/256 of itself. */
static uint16_t dimmed(uint16_t c, int k) {
    k = k < 0 ? 0 : k > 256 ? 256 : k;
    return (uint16_t)((((c >> 11) & 31) * k >> 8) << 11 | (((c >> 5) & 63) * k >> 8) << 5 | ((c & 31) * k >> 8));
}

/* The same number every time for the same (x, y): what makes a block or a
 * row glitch together. */
static uint32_t hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* 0..1 */
static float hashf(int x, int y) { return (hash(x, y) & 0xffff) / 65536.0f; }

/* Smooth noise, 0..1: hashf at the whole numbers, eased between them. */
static float smooth_noise(float x, float y) {
    int xi = (int)x, yi = (int)y;
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hashf(xi, yi), b = hashf(xi + 1, yi), c = hashf(xi, yi + 1), d = hashf(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static inline uint8_t pixel_at(const uint8_t *picture, int x, int y) {
    x = x < 0 ? 0 : x >= FX_W ? FX_W - 1 : x;
    y = y < 0 ? 0 : y >= FX_H ? FX_H - 1 : y;
    return picture[y * FX_W + x];
}

/* Red from one colour, green from another, blue from a third: a picture
 * whose colours have come apart. */
static inline uint16_t split_colors(uint16_t r, uint16_t g, uint16_t b) {
    return (uint16_t)((r & 0xf800) | (g & 0x07e0) | (b & 0x001f));
}

static float infect_was; /* `t` at the last step */

/* Melting: when each column starts to run, in 255ths of MELT_LAST. A column
 * takes the rest of the time (1 - MELT_LAST) to go, so the last is gone
 * just as the transition ends. */
#define MELT_LAST 0.58f
static uint8_t melt_from[FX_W];

void fx_transition_begin(int kind) {
    infect_was = -1;
    if (kind == FX_TR_MELT) {
        /* neighbours start near together, in runs; spread to use all the time */
        float from[FX_W], lo = 1, hi = 0;
        for (int x = 0; x < FX_W; x++) {
            from[x] = smooth_noise(x / 9.0f, 3) + 0.15f * hashf(x, 9);
            lo = from[x] < lo ? from[x] : lo;
            hi = from[x] > hi ? from[x] : hi;
        }
        for (int x = 0; x < FX_W; x++) {
            melt_from[x] = (uint8_t)(255 * (from[x] - lo) / (hi - lo + 0.001f));
        }
    }
}

float fx_transition_energy(int kind, float t) {
    /* hyperspace takes the stars up to light speed itself; Life just needs
     * to breed at a fair rate */
    return kind == FX_TR_HYPERSPACE ? t * t : kind == FX_TR_INFECT ? 0.3f : -1;
}

/*
 * Into Life. Each cell has a moment, which sweeps across the picture in
 * fronts (cells that came alone would die at once). When it comes, the cell
 * is born if the picture is bright there, and that bit of the picture goes;
 * so does any bit a living cell is on.
 */
static void infect_step(const struct fx_transition *tr) {
    for (int y = 0; y < LIFE_H; y++) {
        for (int x = 0; x < LIFE_W; x++) {
            const int cell = y * LIFE_W + x;
            uint8_t *old = &tr->old[y * 2 * FX_W + x * 2];
            const float at = 0.06f + 0.8f * smooth_noise(x / 11.0f, y / 11.0f) + 0.05f * hashf(x, y);
            bool eaten = canvas[cell] != 0;
            if (infect_was < at && at <= tr->t) {
                if (brightness(tr->old_palette[old[0]]) > 45 && (hash(x + 71, y + 13) & 1)) {
                    canvas[cell] = 251;
                }
                eaten = true;
            }
            if (eaten) {
                old[0] = old[1] = old[FX_W] = old[FX_W + 1] = FX_BLACK;
            }
        }
    }
    infect_was = tr->t;
}

void fx_transition_step(const struct fx_transition *tr) {
    if (tr->kind == FX_TR_INFECT) {
        infect_step(tr);
    } else if (tr->kind == FX_TR_MELT) {
        code_clear(); /* nothing is written until the picture has gone */
    }
}

/*
 * Into Code. The picture runs down the screen like wax, column by column,
 * each stretching as it goes (the foot of a column falls faster than its
 * head) and swaying a little, with a dark lip at its top edge. Behind it is
 * the empty page.
 */
static void melt_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const float sway = sinf(y * 0.2f + tr->t * 10) * 2.5f, stretch = 0.58f + 0.42f * y / FX_H;
    for (int x = 0; x < FX_W; x++) {
        const float u = clampf((tr->t - melt_from[x] * (MELT_LAST / 255)) / (1 - MELT_LAST), 0, 1);
        const float sy = y - u * u * 230 * stretch;
        if (sy < 0) {
            out[x] = 0;
            continue;
        }
        const uint16_t c = tr->old_palette[pixel_at(tr->old, x + (int)(sway * u), (int)sy)];
        out[x] = sy < 2 && u > 0 ? dimmed(c, 150) : c;
    }
}

static void infect_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const uint8_t *fb = &tr->fb[y * FX_W], *old = &tr->old[y * FX_W];
    for (int x = 0; x < FX_W; x++) {
        out[x] = fb[x] ? tr->palette[fb[x]] : tr->old_palette[old[x]];
    }
}

/* Which of 140 directions (dx, dy) points in, near enough: arctan by a
 * polynomial, as this is asked for every pixel. */
static int direction(float dx, float dy) {
    const float ax = fabsf(dx), ay = fabsf(dy);
    const float a = ax > ay ? ay / (ax + 0.001f) : ax / (ay + 0.001f);
    float angle = a * (0.7854f + 0.273f * (1 - a));
    angle = ay > ax ? 1.5708f - angle : angle;
    angle = dx < 0 ? 3.14159f - angle : angle;
    angle = dy < 0 ? -angle : angle;
    return (int)((angle + 3.1416f) * (140 / TWO_PI));
}

/*
 * Into Warp. The picture is blown up from the vanishing point, each
 * direction at its own rate, so that it streaks; a hole opens in the middle
 * of it and grows until nothing is left; and it dims. The stars are over it
 * from the start.
 */
static void hyperspace_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const float t = tr->t, zoom = 8.5f * t * t * sqrtf(t), hole = 60 * t * t * t;
    const int dim = (int)(256 * (1 - 0.5f * t));
    const uint8_t *fb = &tr->fb[y * FX_W];
    for (int x = 0; x < FX_W; x++) {
        if (fb[x]) {
            out[x] = tr->palette[fb[x]];
            continue;
        }
        const float dx = x - warp_cx, dy = y - warp_cy;
        const float s = 1 + zoom * (0.25f + 1.75f * hashf(direction(dx, dy), 5));
        const int sx = (int)(warp_cx + dx / s), sy = (int)(warp_cy + dy / s);
        if (sqrtf(dx * dx + dy * dy) < hole * s || (unsigned)sx >= FX_W || (unsigned)sy >= FX_H) {
            out[x] = 0;
        } else {
            out[x] = dimmed(tr->old_palette[tr->old[sy * FX_W + sx]], dim);
        }
    }
}

/*
 * The way out, first part: the running picture breaks up. Lightly at first
 * and in bursts, then harder and nearly all the time: rows torn sideways,
 * the colours apart, every other row dimmed, blocks in the colours of the
 * screen that is coming back, and near the end the picture rolling.
 */
static void glitch_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const float secs = tr->t * (FX_SAVER_OUT_MS / 1000.0f), from = FX_SAVER_GLITCH_FROM_MS / 1000.0f;
    const uint8_t *fb = tr->fb;
    if (secs <= from) {
        for (int x = 0; x < FX_W; x++) {
            out[x] = tr->palette[fb[y * FX_W + x]];
        }
        return;
    }
    const float ramp = (secs - from) / (FX_SAVER_OUT_MS / 1000.0f - from), g = 0.1f + 0.9f * ramp * sqrtf(ramp);
    const int frame = (int)tr->frame;
    const bool burst = hashf(frame / 2, 77) < 0.22f + 0.78f * g;
    const float gb = burst ? g : g * 0.25f;
    const int split = (int)((burst ? 1.5f : 0) + 8 * gb * hashf(frame, 3));
    const bool rolls = g > 0.75f && hashf(frame / 3, 9) < (g - 0.75f) * 2;
    const int yy = rolls ? (y + (int)(hashf(frame / 3, 10) * FX_H)) % FX_H : y;
    const int band = y / 6;
    const int shift =
        hashf(band, frame / 2) < 0.7f * gb ? (int)((hashf(band, frame / 2 + 50) - 0.5f) * (24 + 96 * g)) : 0;
    for (int x = 0; x < FX_W; x++) {
        const int bx = (x + shift + 200) / 14, by = y / 10;
        if (hashf(bx + by * 31, frame / 3) < 0.4f * gb * g) {
            out[x] = tr->old_palette[pixel_at(fb, x + shift, yy)] | (uint16_t)(hash(bx, by) & 0x1863);
            continue;
        }
        out[x] = split_colors(tr->palette[pixel_at(fb, x + shift + split, yy)], tr->palette[pixel_at(fb, x + shift, yy)],
                              tr->palette[pixel_at(fb, x + shift - split, yy)]);
        if (burst && (y & 1)) {
            out[x] = dimmed(out[x], 256 - (int)(120 * g));
        }
    }
}

/* The way out, last part: flat out, the screensaver's still giving way
 * block by block to the screen that is back, and settling at the end. */
static void cut_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const float t = tr->t, g = t < 0.7f ? 1 : (1 - t) / 0.3f;
    const int frame = (int)tr->frame + 1000;
    const int split = (int)(8 * g * hashf(frame, 3));
    const int band = y / 6;
    const int shift = hashf(band, frame) < 0.7f * g ? (int)((hashf(band, frame + 50) - 0.5f) * 120 * g) : 0;
    for (int x = 0; x < FX_W; x++) {
        const int bx = (x + shift + 200) / 14, by = y / 10;
        const float at = 0.05f + 0.75f * hashf(bx + 300, by);
        bool back = t > at;
        if (fabsf(t - at) < 0.1f) {
            back = hash(bx + by * 40, frame) & 1; /* flickers as it goes over */
        }
        if (back) {
            out[x] = tr->palette[pixel_at(tr->fb, x + shift / 2, y)];
        } else if (hashf(bx + by * 31, frame) < 0.4f * g) {
            out[x] = tr->palette[pixel_at(tr->old, x + shift, y)];
        } else {
            out[x] = split_colors(tr->old_palette[pixel_at(tr->old, x + shift + split, y)],
                                  tr->old_palette[pixel_at(tr->old, x + shift, y)],
                                  tr->old_palette[pixel_at(tr->old, x + shift - split, y)]);
            if (y & 1) {
                out[x] = dimmed(out[x], 256 - (int)(120 * g));
            }
        }
    }
}

/* A colour k/256 of the way from `a` to `b`. */
static uint16_t between(uint16_t a, uint16_t b, int k) {
    const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    return (uint16_t)((ar + (((b >> 11) - ar) * k >> 8)) << 11 | (ag + ((((b >> 5) & 63) - ag) * k >> 8)) << 5 |
                      (ab + (((b & 31) - ab) * k >> 8)));
}

/*
 * The panel going off, as a television with a tube did: the picture closes
 * up and down on to a line across the middle, whitening as its light is
 * packed in; the line closes to a dot; the dot dies away. It ends black.
 */
static void crt_off_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const float t = tr->t, from_mid = y - FX_H / 2 + 0.5f;
    const int cx = FX_W / 2;

    memset(out, 0, FX_W * sizeof(*out));
    if (t < 0.45f) {
        float k = t / 0.45f;
        k = k * k;
        const float half = FX_H / 2 * (1 - k) > 1 ? FX_H / 2 * (1 - k) : 1;
        if (fabsf(from_mid) > half) {
            return;
        }
        const int from = FX_H / 2 + (int)(from_mid * (FX_H / 2) / half), white = (int)(k * 256);
        for (int x = 0; x < FX_W; x++) {
            out[x] = between(tr->palette[pixel_at(tr->fb, x, from)], 0xffff, white);
        }
    } else if (t < 0.8f) {
        const float k = (t - 0.45f) / 0.35f;
        const int half = 2 + (int)((FX_W / 2 - 2) * (1 - k) * (1 - k));
        /* the line, and its glow a row either side */
        const uint16_t color = fabsf(from_mid) < 1 ? 0xffff : fabsf(from_mid) < 2 ? rgb(70, 80, 90) : 0;
        for (int x = cx - half; color && x < cx + half; x++) {
            out[x] = color;
        }
    } else if (fabsf(from_mid) < 2) {
        const int light = (int)(255 * (1 - (t - 0.8f) / 0.2f) * (fabsf(from_mid) < 1 ? 1.0f : 0.4f));
        out[cx - 1] = out[cx] = rgb(light, light, light);
        out[cx - 2] = out[cx + 1] = rgb(light / 3, light / 3, light / 3);
    }
}

/* The panel coming on: white noise, with the picture coming through it. */
static void static_row(const struct fx_transition *tr, int y, uint16_t *out) {
    float k = clampf((tr->t - 0.08f) / 0.92f, 0.0f, 1.0f);
    k = k * k * (3 - 2 * k);
    const int through = (int)(k * 256);
    /* the noise in rows that now and then come brighter, or torn sideways */
    const uint32_t row = hash(y, (int)tr->frame);
    const int gain = (row & 15) == 0 ? 256 : 150 + (int)((row >> 4) & 63);
    const int shift = ((row >> 10) & 31) == 0 ? (int)((row >> 15) & 15) - 8 : 0;
    uint32_t r = row | 1;
    for (int x = 0; x < FX_W; x++) {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        const int grey = (int)(r & 255) * gain >> 8;
        const int from = x + shift * (256 - through) / 256;
        out[x] = between(rgb(grey, grey, grey), tr->palette[pixel_at(tr->fb, from, y)], through);
    }
}

void fx_transition_row(const struct fx_transition *tr, int y, uint16_t *out) {
    switch (tr->kind) {
    case FX_TR_HYPERSPACE:
        hyperspace_row(tr, y, out);
        break;
    case FX_TR_INFECT:
        infect_row(tr, y, out);
        break;
    case FX_TR_MELT:
        melt_row(tr, y, out);
        break;
    case FX_TR_GLITCH:
        glitch_row(tr, y, out);
        break;
    case FX_TR_CUT:
        cut_row(tr, y, out);
        break;
    case FX_TR_CRT_OFF:
        crt_off_row(tr, y, out);
        break;
    case FX_TR_STATIC:
        static_row(tr, y, out);
        break;
    default:
        for (int x = 0; x < FX_W; x++) {
            out[x] = tr->palette[tr->fb[y * FX_W + x]];
        }
    }
}
