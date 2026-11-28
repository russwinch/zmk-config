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

/* The board's shape, for the Keys screen (fx_set_board). */
#include "boards.h"
static const struct fx_board *shape = &fx_boards[FX_BOARD_URCHIN];

void fx_set_board(int which) {
    if (which >= 0 && which < FX_BOARDS) {
        shape = &fx_boards[which];
    }
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
 * dongle itself; a table of the three Bluetooth links; the profile in use, USB
 * and the output in use; layer, typing speed and modifiers; then the last
 * few raw events beside a map of the keys (`board`), and the worst key
 * bounce seen.
 */

enum {
    DBG_BG, DBG_DIM, DBG_LABEL, DBG_TEXT, DBG_BRIGHT, DBG_GOOD, DBG_WARN, DBG_BAD,
    DBG_MOD,   /* a key holding a modifier down */
    DBG_LAYER, /* a key holding a layer on */
    DBG_HEAT,  /* ..DBG_HEAT + 15 */
    DBG_BODY = DBG_HEAT + 16, /* a board's PCB, on the Keys screen */
    DBG_EDGE,                 /* and its cover, and the edge round a screen */
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
    palette[DBG_BODY] = color565(mix(scheme->bg, scheme->dim, 95));
    palette[DBG_EDGE] = color565(mix(scheme->bg, scheme->dim, 35));
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


/*
 * How a link's signal is coloured, on the Debug and Keys screens. The
 * nRF52840 still hears a packet at about -95 dBm (1M PHY). Across a desk
 * -50 to -80 is usual and loses nothing, so that is green; amber, within
 * 10 dB, is where a hand or a body in the way starts to cost packets (sent
 * again, so later): worth moving the dongle; red, within 5 dB, is where
 * the link drops.
 */
#define SIGNAL_GOOD_DBM -85 /* above it: green */
#define SIGNAL_WARN_DBM -90 /* above it: amber; red at and below */

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

/* The table's columns: the left half, the dongle, the right half, each
 * DBG_COL_W across from DBG_COL_X(i), after the rows' labels; a chart in each. */
#define DBG_COL_X(i) (34 + (i) * 35)
#define DBG_COL_W 33
/* and its rows, each chart under its reading */
#define DBG_ROW_DBM 29
#define DBG_ROW_DROPS 43
#define DBG_ROW_LEVEL 51
#define DBG_ROW_V 59
#define DBG_ROW_MW 73
#define DBG_ROW_CHG 81
#define DBG_CHART_W 30
#define DBG_CHART_H 5

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
/* The keys pressed and encoders turned this frame, for `board` and `shape_board`. */
static void board_heat(const struct fx_input *in) {
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
}

/* And the glow going, two seconds after. */
static void board_fade(const struct fx_input *in) {
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

/* A half that isn't connected: the colour of all its keys, knobs and
 * rollers then (red; grey asleep, flashing green while it is updated), or 0
 * (DBG_BG) when it is connected. */
#define AWAY_FLASH 30 /* 30ths of a second: green two thirds of it */
static float away_clock; /* timed in fx_frame */
static uint8_t half_down(const struct fx_debug *d, int half) {
    const struct fx_link *link = &d->half[half];
    return link->up                       ? DBG_BG
           : link->away == FX_AWAY_SLEEP  ? DBG_LABEL
           : link->away == FX_AWAY_UPDATE ? ((int)away_clock % AWAY_FLASH < AWAY_FLASH * 2 / 3 ? DBG_GOOD : DBG_DIM)
                                          : DBG_BAD;
}

/* And what to say in place of its signal: why it is away, or NULL. */
static const char *away_word(const struct fx_link *link, uint8_t *color) {
    if (link->up || !link->away) {
        return NULL;
    }
    *color = link->away == FX_AWAY_SLEEP ? DBG_LABEL : DBG_GOOD;
    return link->away == FX_AWAY_SLEEP ? "SLEEP" : "UPDATE";
}

/* A key's colour: white held (orange holding a modifier, violet a layer), glowing after. */
static uint8_t key_color(const struct fx_debug *d, int key) {
    const uint64_t bit = 1ull << key;
    return !(d->held & bit)     ? (debug_heat[key] ? DBG_HEAT + debug_heat[key] : DBG_DIM)
           : d->held_layer & bit ? DBG_LAYER
           : d->held_mod & bit   ? DBG_MOD
                                 : DBG_BRIGHT;
}

static void board(uint8_t *fb, const struct fx_input *in, int left, int top, int pitch, int high, int gap,
                  int split) {
    const struct fx_debug *d = in->debug;
    board_heat(in);
    for (int key = 0; key < key_count && key < (int)sizeof(debug_heat); key++) {
        const int col = board_col(key), row = board_row(key) < 4 ? board_row(key) : 4;
        const int kx = left + col * pitch + (col >= board_cols / 2 ? split : 0), ky = top + row * (high + gap);
        const uint64_t bit = 1ull << key;
        const bool held = (d->held & bit) != 0;
        const uint8_t down = half_down(d, col >= board_cols / 2);
        const uint8_t color = down ? down : key_color(d, key);
        const int encoder = key_encoder(key);
        if (down && encoder >= 0) {
            knob(fb, kx, ky, pitch - gap, high, DBG_BG, down, down, knob_shown[encoder]);
            continue;
        }
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
    board_fade(in);
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

/* A little chart at (x, y), DBG_CHART_W across and DBG_CHART_H high, as
 * Keys had them: each reading a column `high` of DBG_CHART_H up from a dim line along the foot
 * (0: not known, nothing), dim topped in `color[i]`, or solid when that is
 * amber or red, so the bad minutes stand out. */
static void chart(uint8_t *fb, int x, int y, const uint8_t *high, const uint8_t *color) {
    for (int i = 0; i < DBG_CHART_W; i++) {
        const int h = high[i] > DBG_CHART_H ? DBG_CHART_H : high[i];
        const bool trouble = color[i] == DBG_WARN || color[i] == DBG_BAD;
        plot(fb, x + i, y + DBG_CHART_H - 1, DBG_DIM);
        for (int up = 0; up < h; up++) {
            plot(fb, x + i, y + DBG_CHART_H - 1 - up, trouble || up == h - 1 ? color[i] : DBG_DIM);
        }
    }
}

/* A link's last DBG_CHART_W minutes of signal (`log`, fx_debug's conn_log,
 * a minute a slot): the weakest of each, -95 dBm a column one up and -40
 * the full height, white, amber or red; down, red at the top; over USB, green at
 * the top; idle, nothing. */
static void signal_chart(uint8_t *fb, int x, int y, const int8_t *log) {
    uint8_t high[DBG_CHART_W], color[DBG_CHART_W];
    for (int i = 0; i < DBG_CHART_W; i++) {
        const int8_t worst = log[FX_CONN_HISTORY - DBG_CHART_W + i];
        if (worst == FX_SIGNAL_DOWN || worst == FX_CONN_USB) {
            high[i] = DBG_CHART_H;
            color[i] = worst == FX_CONN_USB ? DBG_TEXT : DBG_BAD;
        } else if (worst < 0) {
            const int h = 1 + (worst + 95) * (DBG_CHART_H - 1) / 55;
            high[i] = (uint8_t)(h < 1 ? 1 : h > DBG_CHART_H ? DBG_CHART_H : h);
            color[i] = worst > SIGNAL_GOOD_DBM ? DBG_TEXT : worst > SIGNAL_WARN_DBM ? DBG_WARN : DBG_BAD;
        } else {
            high[i] = 0;
            color[i] = DBG_DIM;
        }
    }
    chart(fb, x, y, high, color);
}

/* The last DBG_CHART_W slots of volts (`log`, fx_debug's mv_log), from the
 * lowest of them to the highest, or `span` mV if that is more, so a steady
 * supply stays flat and a battery running down shows. */
static void mv_chart(uint8_t *fb, int x, int y, const uint16_t *log, int span) {
    uint8_t high[DBG_CHART_W], color[DBG_CHART_W];
    int low = 0, top = 0;
    for (int i = 0; i < DBG_CHART_W; i++) {
        const int mv = log[FX_MV_HISTORY - DBG_CHART_W + i];
        low = mv && (low == 0 || mv < low) ? mv : low;
        top = mv > top ? mv : top;
    }
    if (top - low < span) {
        low = (low + top - span) / 2;
        top = low + span;
    }
    for (int i = 0; i < DBG_CHART_W; i++) {
        const int mv = log[FX_MV_HISTORY - DBG_CHART_W + i];
        const int h = mv ? 1 + (mv - low) * (DBG_CHART_H - 1) / (top - low) : 0;
        high[i] = (uint8_t)(mv == 0 ? 0 : h < 1 ? 1 : h > DBG_CHART_H ? DBG_CHART_H : h);
        color[i] = DBG_TEXT;
    }
    chart(fb, x, y, high, color);
}

/* Text centred in column `col` of the table. */
static void put_col(uint8_t *fb, int col, int y, const char *s, uint8_t color) {
    put(fb, DBG_COL_X(col) + (DBG_COL_W - (5 * (int)strlen(s) - 1)) / 2, y, s, color);
}

/* A link's signal as text and its colour: dBm, `--` when down, or why it
 * couldn't be read (EH no handle for the link, EB no command buffer, E1-E99
 * the radio's or Zephyr's error). */
static uint8_t signal_text(char *buf, size_t size, const struct fx_link *link) {
    uint8_t away;
    const char *word = away_word(link, &away);
    if (word) {
        snprintf(buf, size, "%s", word);
        return away;
    }
    if (!link->up) {
        snprintf(buf, size, "--");
        return DBG_BAD;
    }
    if (link->rssi_known) {
        snprintf(buf, size, "%d", link->rssi < -99 ? -99 : link->rssi);
        return signal_color(link);
    }
    if (link->rssi_err == FX_RSSI_ERR_HANDLE) {
        snprintf(buf, size, "EH");
    } else if (link->rssi_err == FX_RSSI_ERR_BUFFER) {
        snprintf(buf, size, "EB");
    } else if (link->rssi_err) {
        snprintf(buf, size, "E%u", link->rssi_err > 99 ? 99 : link->rssi_err);
    } else {
        buf[0] = '\0';
    }
    return DBG_BAD;
}

/* Millivolts as volts to the millivolt, or why not (`why`, else "--"). */
static void mv_text(char *buf, size_t size, unsigned mv, const char *why) {
    if (mv) {
        mv = mv > 9999 ? 9999 : mv;
        snprintf(buf, size, "%u.%03u", mv / 1000, mv % 1000);
    } else {
        snprintf(buf, size, "%s", why && why[0] ? why : "--");
    }
}

/* The output's waves, as over the animations, 7 by 5 from (x, y). */
static void waves(uint8_t *fb, int x, int y, uint8_t color) {
    static const char *const rows[5] = {".ooooo.", "o.....o", "..ooo..", ".o...o.", "...o..."};
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 7; col++) {
            if (rows[row][col] == 'o') {
                plot(fb, x + col, y + row, color);
            }
        }
    }
}

static bool profile_flash(const struct fx_input *in, bool *off);

/* The Bluetooth profile in use, from (x, y), PROFILE_MARK_W across: the
 * waves and its number, green when its computer is connected, dim when
 * nothing is paired on it; flashing for a moment when it has just been
 * picked. */
#define PROFILE_MARK_W 13
static void profile_mark(uint8_t *fb, const struct fx_input *in, int x, int y) {
    bool off;
    if (in->status == NULL || (profile_flash(in, &off) && off)) {
        return;
    }
    char buf[4];
    const uint8_t color = in->status->profile_connected ? DBG_GOOD : in->status->host_open ? DBG_LABEL : DBG_TEXT;
    waves(fb, x, y + 1, color);
    snprintf(buf, sizeof(buf), "%u", in->status->profile);
    put(fb, x + 9, y, buf, color);
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
 * while rough (an upper limit) one place fewer, after a space for the < in front. */
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

/*
 * Whether each part of the keyboard works, and what went wrong; the typing
 * speed and the batteries' levels are on Typist and Status. Top to bottom:
 * the dongle (build, up time, activity, memory; why it last restarted, the
 * quickest a key has bounced); a table with a column for the left half, the
 * dongle and the right half, as on Status: each link's signal with its last
 * 90 seconds, drops and why the last; each half's battery volts and the
 * dongle's supply with their last 90 minutes, the charging-state reads and
 * the dongle's temperature, the draw; the profile in use and USB, the reports sent
 * over it; the layer and modifiers, the last event, and every key, lit while
 * held.
 */
static void debug_frame(uint8_t *fb, const struct fx_input *in) {
    static const struct fx_debug none;
    const struct fx_debug *d = in->debug ? in->debug : &none;
    char buf[32];

    memset(fb, DBG_BG, PIXELS);

    /* the dongle: which build, how long up, active, idle or asleep, and the
     * ms this screen takes a frame. The top line is in the corners' curve, so it starts
     * further in and ends as far from the right */
    const int top_inset = fx_corner_inset(4, 10);
    int x = put(fb, top_inset, 4, d->firmware ? d->firmware : "DEV", DBG_BRIGHT) + 4;
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)(d->uptime_s / 3600), (unsigned)(d->uptime_s / 60 % 60),
             (unsigned)(d->uptime_s % 60));
    x = put(fb, x, 4, buf, DBG_TEXT) + 4;
    put(fb, x, 4, d->activity == 0 ? "ACT" : d->activity == 1 ? "IDLE" : "SLEEP", d->activity ? DBG_WARN : DBG_GOOD);
    snprintf(buf, sizeof(buf), "%u", d->frame_ms);
    put_right(fb, FX_W - top_inset - 10 - 1, 4, buf, DBG_TEXT);
    put_right(fb, FX_W - top_inset, 4, "ms", DBG_LABEL);

    /* why it last restarted, a hang or crash in red with its code (and where);
     * then the quickest a key has come back down after going up: a switch
     * that bounces shows here as a few ms, red under 30 */
    const int y2 = 12;
    const bool crash_pc = d->crash == FX_CRASH_FAULT && d->crash_pc;
    x = put(fb, 6, y2, crash_pc ? "" : "RESET", DBG_LABEL) + (crash_pc ? 0 : 3);
    if (d->crash == FX_CRASH_HANG) {
        x = put(fb, x, y2, "HANG", DBG_BAD) + 3;
        snprintf(buf, sizeof(buf), "%u", (unsigned)d->crash_code);
        x = put(fb, x, y2, buf, DBG_LABEL) + 3;
    } else if (d->crash == FX_CRASH_FAULT) {
        x = put(fb, x, y2, "CRASH", DBG_BAD) + 3;
        snprintf(buf, sizeof(buf), "%02X", (unsigned)d->crash_code);
        x = put(fb, x, y2, buf, DBG_LABEL) + 3;
        if (crash_pc) {
            /* in flash, 5 hex digits; anywhere else (RAM) won't fit */
            if (d->crash_pc < 0x100000) {
                snprintf(buf, sizeof(buf), "%05X", (unsigned)d->crash_pc);
            } else {
                strcpy(buf, "RAM");
            }
            x = put(fb, x, y2, buf, DBG_TEXT) + 3;
        }
    } else {
        const bool fault = (d->reset_cause & (1 << 4 | 1 << 8 | 1 << 2)) != 0;
        x = put(fb, x, y2, reset_name(d->reset_cause), fault ? DBG_BAD : DBG_TEXT) + 3;
        snprintf(buf, sizeof(buf), "%02X", (unsigned)(d->reset_cause & 0xff)); /* all the reasons, in hex */
        put(fb, x, y2, buf, DBG_LABEL);
    }
    if (d->bounce_ms) {
        snprintf(buf, sizeof(buf), "%ums", d->bounce_ms);
        put_right(fb, FX_W - 6, y2, buf, d->bounce_ms < 30 ? DBG_WARN : DBG_TEXT); /* a hint, not proof: see the README */
        put_right(fb, FX_W - 6 - 5 * (int)strlen(buf) - 3, y2, "BOUNCE", DBG_LABEL);
    } else {
        put_right(fb, FX_W - 6, y2, "BOUNCE --", DBG_LABEL);
    }

    /* the table: a column each for the left half, the dongle and the right
     * half, as on Status; a row for each reading, its unit or what it is in
     * front, the charts under their readings a minute a dot. First the links
     * (the dongle's to the computer): signal, drops and why the last */
    const bool usb_out = in->status && in->status->usb;
    const struct fx_link *links[3] = {&d->half[0], &d->host, &d->half[1]};
    const int8_t *logs[3] = {d->conn_log[0], d->conn_log[2], d->conn_log[1]};
    /* the dongle's column under the keyboard's name (it is that keyboard's
     * dongle), with room either side of it for up to 11 letters; a name too
     * long loses words from the front (AURORA CORNE: CORNE), then letters */
    char name[12] = "DONGLE";
    if (d->keyboard) {
        const char *from = d->keyboard;
        while (strlen(from) > sizeof(name) - 1 && strchr(from, ' ') != NULL) {
            from = strchr(from, ' ') + 1;
        }
        snprintf(name, sizeof(name), "%s", from);
        for (char *c = name; *c; c++) {
            *c = *c >= 'a' && *c <= 'z' ? *c - 32 : *c;
        }
    }
    const char *titles[3] = {"LEFT", name, "RIGHT"};
    const int title_y = 21;
    for (int col = 0; col < 3; col++) {
        /* the halves' out to their sides, to leave the name room */
        const int wide = 5 * (int)strlen(titles[col]) - 1;
        const int left = col == 0 ? DBG_COL_X(0) : col == 2 ? DBG_COL_X(2) + DBG_COL_W - wide
                                                            : DBG_COL_X(1) + (DBG_COL_W - wide) / 2;
        const uint8_t down = col == 1 ? (links[col]->up || usb_out ? DBG_BG : DBG_BAD) : half_down(d, col == 2);
        put(fb, left, title_y, titles[col], down ? down : DBG_BRIGHT);
    }
    static const char *const labels[] = {"dBm", "DROPS", "%", "V", "mW", "CHG"};
    static const int label_y[] = {DBG_ROW_DBM, DBG_ROW_DROPS, DBG_ROW_LEVEL, DBG_ROW_V, DBG_ROW_MW, DBG_ROW_CHG};
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        put(fb, 5, label_y[i], labels[i], DBG_LABEL);
    }
    for (int col = 0; col < 3; col++) {
        const struct fx_link *link = links[col];
        if (col == 1 && usb_out) {
            put_col(fb, col, DBG_ROW_DBM, "USB", DBG_LABEL); /* no radio to the computer */
        } else {
            const uint8_t color = signal_text(buf, sizeof(buf), link);
            put_col(fb, col, DBG_ROW_DBM, buf, color);
        }
        signal_chart(fb, DBG_COL_X(col) + 1, DBG_ROW_DBM + 8, logs[col]);
        /* the drops, and why the last: 08 went silent, 13 the other end
         * closed it, 16 the dongle did, 3E never got going */
        snprintf(buf, sizeof(buf), "%d", link->drops > 99 ? 99 : link->drops);
        if (link->reason) {
            const int n = (int)strlen(buf);
            snprintf(buf + n, sizeof(buf) - n, " %02X", link->reason);
            const int left = DBG_COL_X(col) + (DBG_COL_W - (5 * (int)strlen(buf) - 1)) / 2;
            buf[n] = '\0';
            put(fb, left, DBG_ROW_DROPS, buf, link->drops ? DBG_WARN : DBG_LABEL);
            snprintf(buf, sizeof(buf), "%02X", link->reason);
            put(fb, left + 5 * (n + 1), DBG_ROW_DROPS, buf, DBG_LABEL);
        } else {
            put_col(fb, col, DBG_ROW_DROPS, buf, link->drops ? DBG_WARN : DBG_LABEL);
        }
    }

    /* then the batteries: each half's level, its volts (the dongle's 3.3 V
     * supply in its column) to the millivolt and their last 30 minutes, the
     * halves' average draw and charging-state subscription (in the dongle's
     * column, its temperature) */
    const uint16_t mv[3] = {d->half_mv[0], d->dongle_mv, d->half_mv[1]};
    const uint16_t *mv_logs[3] = {d->mv_log[0], d->mv_log[2], d->mv_log[1]};
    for (int col = 0; col < 3; col++) {
        const int half = col / 2; /* the left's or the right's, for cols 0 and 2 */
        const bool charging = col != 1 && in->status && in->status->charging[half];
        mv_text(buf, sizeof(buf), mv[col], col == 1 ? d->dongle_mv_why : NULL);
        put_col(fb, col, DBG_ROW_V, buf, mv[col] && !charging ? DBG_TEXT : col == 1 && d->dongle_mv_why[0] ? DBG_WARN : DBG_LABEL);
        mv_chart(fb, DBG_COL_X(col) + 1, DBG_ROW_V + 8, mv_logs[col], 60);
        if (col != 1) {
            /* the level (the dongle's estimate while charging, as
             * ZMK's reads high), a bolt after it while on USB power */
            bool estimate;
            const uint8_t level = half_level(in, half, &estimate);
            if (level) {
                snprintf(buf, sizeof(buf), "%u", level);
            } else {
                strcpy(buf, "--");
            }
            const int left = DBG_COL_X(col) + (DBG_COL_W - (5 * (int)strlen(buf) - 1)) / 2;
            put(fb, left, DBG_ROW_LEVEL, buf, level ? battery_color(level) : DBG_LABEL);
            charging_bolt(fb, left + 5 * (int)strlen(buf), DBG_ROW_LEVEL, in->status, half);
        }
        if (col == 1) {
            if (d->dongle_temp_known) {
                const int c = d->dongle_temp_c < -9 ? -9 : d->dongle_temp_c > 99 ? 99 : d->dongle_temp_c;
                snprintf(buf, sizeof(buf), "%dC", c);
                put_col(fb, col, DBG_ROW_CHG, buf, c >= 60 ? DBG_WARN : DBG_TEXT);
            } else {
                snprintf(buf, sizeof(buf), "%sC", d->dongle_temp_why[0] ? d->dongle_temp_why : "--");
                put_col(fb, col, DBG_ROW_CHG, buf, d->dongle_temp_why[0] ? DBG_WARN : DBG_LABEL);
            }
            continue;
        }
        /* the charging state: where the dongle has got with it (see
         * dongle_half_info_progress): OK green, WT/Q/R/S on the way in amber,
         * NF (firmware without it) dim, E and its error or TO in red; a
         * bolt after it while on USB power */
        const char *reads = d->half_reads[half];
        const bool bad = reads[0] == 'E' || reads[0] == 'T';
        if (reads[0] == 'E' && d->half_err[half]) {
            snprintf(buf, sizeof(buf), "E%02X", d->half_err[half]);
        } else {
            snprintf(buf, sizeof(buf), "%s", reads[0] ? reads : "--");
        }
        put_col(fb, col, DBG_ROW_CHG, buf,
                bad ? DBG_BAD : reads[0] == 'O' ? DBG_GOOD : reads[0] == 'N' || !reads[0] ? DBG_LABEL : DBG_WARN);
        const int hundredths = d->drain_hundredths_mw[half];
        const bool rough = hundredths >= 0 && d->drain_rough[half];
        drain_text(buf, sizeof(buf), hundredths, rough);
        put_col(fb, col, DBG_ROW_MW, buf, drain_color(hundredths, rough, d->battery_mah));
        if (rough) {
            put(fb, DBG_COL_X(col) + (DBG_COL_W - (5 * (int)strlen(buf) - 1)) / 2, DBG_ROW_MW, "<", DBG_WARN);
        }
    }

    /* the output: the Bluetooth profile in use and USB's state */
    const int out_y = 89;
    profile_mark(fb, in, 6, out_y);
    x = put(fb, 48, out_y, "USB", DBG_LABEL) + 3;
    put(fb, x, out_y, d->usb == 2 ? "HID" : d->usb == 1 ? "PWR" : "--",
        d->usb == 2 ? (usb_out && in->status->host_connected ? DBG_GOOD : DBG_TEXT) : DBG_TEXT);

    /* LVGL's memory used; then, only if there are any, the USB reports the
     * computer didn't take (UE, as on Status) */
    x = 6;
    if (d->heap_known) {
        x = put(fb, x, out_y + 8, "MEM", DBG_LABEL) + 3;
        snprintf(buf, sizeof(buf), "%u%%", d->heap_used);
        x = put(fb, x, out_y + 8, buf, d->heap_used > 90 ? DBG_BAD : DBG_TEXT) + 3;
    }
    if (d->usb_errors) {
        snprintf(buf, sizeof(buf), "UE%u", d->usb_errors > 99 ? 99 : d->usb_errors);
        x = put(fb, x, out_y + 8, buf, DBG_WARN) + 3;
    }

    /* the layer (number and name), the modifiers lit while held; in the
     * bottom corner's curve, so in from the side */
    const int layer_y = out_y + 16;
    snprintf(buf, sizeof(buf), "L%u", d->layer);
    x = put(fb, fx_corner_inset(layer_y, layer_y + 6) + 1, layer_y, buf, DBG_BRIGHT) + 3;
    if (d->layer_name) {
        snprintf(buf, sizeof(buf), "%.5s", d->layer_name);
        for (char *c = buf; *c; c++) {
            *c = *c >= 'a' && *c <= 'z' ? *c - 32 : *c; /* the small letters are for typed text */
        }
        put(fb, x, layer_y, buf, DBG_TEXT);
    }
    static const char mod_glyph[4] = {FX_GLYPH_CTRL, FX_GLYPH_ALT, FX_GLYPH_SHIFT, FX_GLYPH_GUI};
    static const uint8_t mod_bit[4] = {0x11, 0x44, 0x22, 0x88}; /* HID bits, left and right */
    const int mods_x = 52;
    for (int i = 0; i < 4; i++) {
        const bool on = (d->mods & mod_bit[i]) != 0;
        const int gx = mods_x + i * 7;
        if (on) {
            for (int dy = -1; dy <= 7; dy++) {
                line(fb, gx - 1, layer_y + dy, gx + 5, layer_y + dy, DBG_TEXT);
            }
        }
        const char g[2] = {mod_glyph[i], '\0'};
        put(fb, gx, layer_y, g, on ? DBG_BG : DBG_LABEL);
    }

    /* the last raw event: the ms since the one before, then what it was */
    const int event_y = layer_y + 8;
    if (d->n_log) {
        event_text(buf, sizeof(buf), &d->log[d->n_log - 1]);
        put(fb, fx_corner_inset(event_y, event_y + 6) + 1, event_y, buf, DBG_BRIGHT);
    }

    /* every key, lit while held: one that doesn't light isn't reaching the dongle */
    const int board_pitch = board_cols > 10 ? 4 : 5;
    const int board_left = FX_W - 1 - fx_corner_inset(out_y, out_y + 20) - board_cols * board_pitch;
    board(fb, in, board_left, out_y + 1, board_pitch, 4, 1, 1);
}


static void fill(uint8_t *fb, int x, int y, int wide, int high, uint8_t color);

/* --- a board as it is: each key where it is on it, from boards.h ------------- */

/* How a board's quarter millimetres land on the screen. */
struct view {
    float scale;           /* pixels a quarter millimetre */
    float left_x, right_x; /* where each half's inner edge (x 0) is */
    float top;             /* where y 0 is */
};

/* Fill a polygon of `n` points (x, y, x, y.. in pixels), by pixel middles. */
static void fill_poly(uint8_t *fb, const float *pts, int n, uint8_t color) {
    float top = FX_H, bottom = 0;
    for (int i = 0; i < n; i++) {
        top = pts[2 * i + 1] < top ? pts[2 * i + 1] : top;
        bottom = pts[2 * i + 1] > bottom ? pts[2 * i + 1] : bottom;
    }
    for (int y = top < 0 ? 0 : (int)top; y < FX_H && y <= (int)bottom; y++) {
        const float yc = y + 0.5f;
        float xs[16];
        int m = 0;
        for (int i = 0; i < n && m < 16; i++) {
            const float *a = pts + 2 * i, *b = pts + 2 * ((i + 1) % n);
            if ((a[1] <= yc) != (b[1] <= yc)) {
                const float x = a[0] + (yc - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
                int j = m++;
                for (; j > 0 && xs[j - 1] > x; j--) {
                    xs[j] = xs[j - 1];
                }
                xs[j] = x;
            }
        }
        for (int k = 0; k + 1 < m; k += 2) {
            for (int x = (int)ceilf(xs[k] - 0.5f); x <= (int)floorf(xs[k + 1] - 0.5f); x++) {
                plot(fb, x, y, color);
            }
        }
    }
}

/* A part's corners in pixels, `inset` in from each side. */
static void part_quad(const struct view *v, const struct fx_part *p, bool right, float inset, float out[8]) {
    const float cx = right ? v->right_x - p->x * v->scale : v->left_x + p->x * v->scale, cy = v->top + p->y * v->scale;
    const float a = (right ? -p->rot : p->rot) * (TWO_PI / 360), c = cosf(a), s = sinf(a);
    const float hw = p->w * v->scale / 2 - inset, hh = p->h * v->scale / 2 - inset;
    static const int8_t corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; i++) {
        const float u = corner[i][0] * hw, w = corner[i][1] * hh;
        out[2 * i] = cx + u * c - w * s;
        out[2 * i + 1] = cy + u * s + w * c;
    }
}

/* A half's outline (the left's points, mirrored for the right) in pixels. */
static int outline_px(const struct view *v, const int16_t *pts, int n, bool right, float *out) {
    for (int i = 0; i < n; i++) {
        out[2 * i] = right ? v->right_x - pts[2 * i] * v->scale : v->left_x + pts[2 * i] * v->scale;
        out[2 * i + 1] = v->top + pts[2 * i + 1] * v->scale;
    }
    return n;
}

/* Fit the board into (x, y, wide, high) with `gap` pixels between the halves:
 * only its keys, or (`whole`) its outline and all. */
static struct view shape_view(int x, int y, int wide, int high, int gap, bool whole) {
    float lo_x = 0, hi_x = -10000, lo_y = 10000, hi_y = -10000;
    struct view unit = {1, 0, 0, 0};
    for (int i = 0; i < shape->n_keys; i++) {
        float q[8];
        part_quad(&unit, &shape->keys[i], false, 0, q);
        for (int c = 0; c < 4; c++) {
            lo_x = q[2 * c] < lo_x ? q[2 * c] : lo_x;
            hi_x = q[2 * c] > hi_x ? q[2 * c] : hi_x;
            lo_y = q[2 * c + 1] < lo_y ? q[2 * c + 1] : lo_y;
            hi_y = q[2 * c + 1] > hi_y ? q[2 * c + 1] : hi_y;
        }
    }
    for (int i = 0; whole && i < shape->n_outline; i++) {
        const float px = shape->outline[2 * i], py = shape->outline[2 * i + 1];
        lo_x = px < lo_x ? px : lo_x;
        hi_x = px > hi_x ? px : hi_x;
        lo_y = py < lo_y ? py : lo_y;
        hi_y = py > hi_y ? py : hi_y;
    }
    const float across = (wide - gap) / (2 * (hi_x - lo_x)), down = high / (hi_y - lo_y);
    struct view v = {across < down ? across : down, 0, 0, 0};
    v.left_x = x + wide / 2.0f - gap / 2.0f - hi_x * v.scale;
    v.right_x = x + wide / 2.0f + gap / 2.0f + hi_x * v.scale;
    v.top = y + (high - (hi_y - lo_y) * v.scale) / 2 - lo_y * v.scale;
    /* the halves mirror each other about a whole pixel's edge, so the right
     * half is the left pixel for pixel */
    const float axis = roundf(v.left_x + v.right_x);
    v.left_x = roundf(v.left_x);
    v.right_x = axis - v.left_x;
    return v;
}

/* Where the keys square to the screen go: on a grid, the same pixels apart
 * across and down, each column's keys where the board has its column. The
 * board's pitch is the average spacing of its columns and of its rows. */
struct grid {
    float col_q, row_q; /* the pitch, quarter mm */
    float inner_q;      /* the innermost column's x */
    float cap_q;        /* a 1u key's size */
};

static bool grid_key(const struct fx_part *p) {
    return (p->kind & FX_PART_KIND) == FX_PART_KEY && p->rot == 0 && !(p->kind & FX_PART_RIGHT);
}

static struct grid shape_grid(void) {
    struct grid g = {0, 0, -10000, 10000};
    float outer = 10000;
    for (int i = 0; i < shape->n_keys; i++) {
        const struct fx_part *p = &shape->keys[i];
        if (!grid_key(p)) {
            continue;
        }
        g.inner_q = p->x > g.inner_q ? p->x : g.inner_q;
        g.cap_q = p->w < g.cap_q ? p->w : g.cap_q;
        outer = p->x < outer ? p->x : outer;
    }
    /* how many columns */
    float seen[16];
    int n_seen = 0;
    for (int i = 0; i < shape->n_keys; i++) {
        const struct fx_part *p = &shape->keys[i];
        bool done = false;
        for (int k = 0; k < n_seen; k++) {
            done |= fabsf(seen[k] - p->x) < 8;
        }
        if (!grid_key(p) || done || n_seen == 16) {
            continue;
        }
        seen[n_seen++] = p->x;
    }
    g.col_q = n_seen > 1 ? (g.inner_q - outer) / (n_seen - 1) : 76;
    /* and the rows: the average spacing of keys one above another */
    float count = 0;
    for (int i = 0; i < shape->n_keys; i++) {
        const struct fx_part *p = &shape->keys[i];
        for (int j = 0; grid_key(p) && j < shape->n_keys; j++) {
            const struct fx_part *q = &shape->keys[j];
            const float dy = q->y - p->y;
            if (grid_key(q) && fabsf(q->x - p->x) < 8 && dy > 8 && dy < 1.5f * p->h + 16) {
                g.row_q += dy;
                count++;
            }
        }
    }
    g.row_q = count ? g.row_q / count : g.col_q;
    return g;
}

/* A part square to the screen as whole pixels (x, y, wide, high), worked
 * out on the left half and mirrored for the right: a key on the grid, `g`,
 * anything else where it is. */
static void part_rect(const struct view *v, const struct grid *g, const struct fx_part *p, int *x, int *y, int *wide,
                      int *high) {
    const bool right = p->kind & FX_PART_RIGHT;
    if ((p->kind & FX_PART_KIND) == FX_PART_KEY && g) {
        const int col_px = (int)lroundf(g->col_q * v->scale), row_px = (int)lroundf(g->row_q * v->scale);
        const int col = (int)lroundf((g->inner_q - p->x) / g->col_q);
        /* the column's top key, and how many rows down this one is */
        float top_q = p->y;
        for (int j = 0; j < shape->n_keys; j++) {
            const struct fx_part *q = &shape->keys[j];
            if (grid_key(q) && fabsf(q->x - p->x) < 8) {
                top_q = q->y < top_q ? q->y : top_q;
            }
        }
        const int row = (int)lroundf((p->y - top_q) / g->row_q);
        /* a 1u key the pitch less a pixel, so the gaps are one pixel across and down */
        *wide = (int)lroundf((p->w + g->col_q - g->cap_q) / g->col_q * col_px) - 1;
        *high = (int)lroundf((p->h + g->row_q - g->cap_q) / g->row_q * row_px) - 1;
        const int cx = (int)lroundf(v->left_x + g->inner_q * v->scale) - col * col_px;
        const int cy = (int)lroundf(v->top + top_q * v->scale) + row * row_px;
        *x = cx - *wide / 2;
        *y = cy - *high / 2;
    } else {
        *wide = (int)lroundf(p->w * v->scale) - 1;
        *high = (int)lroundf(p->h * v->scale) - 1;
        if ((p->kind & FX_PART_KIND) == FX_PART_ROLLER && *wide < 6) {
            *wide = 6; /* room for two ribs rolling, so the way it turns shows */
        }
        *x = (int)lroundf(v->left_x + p->x * v->scale - *wide / 2.0f);
        *y = (int)lroundf(v->top + p->y * v->scale - *high / 2.0f);
    }
    if (right) {
        *x = (int)lroundf(v->left_x + v->right_x) - *x - *wide;
    }
}

/* The board drawn as it is through `v`: keys coloured as `board` has them,
 * encoders as knobs (a roller's ribs move with it); with `whole`, its PCB,
 * the cover over its controller, its screen and controller too. */
static void shape_board(uint8_t *fb, const struct fx_input *in, const struct view *v, bool whole) {
    const struct fx_debug *d = in->debug;
    float pts[2 * 48];
    board_heat(in);
    for (int right = 0; whole && right < 2; right++) {
        fill_poly(fb, pts, outline_px(v, shape->outline, shape->n_outline < 48 ? shape->n_outline : 48, right, pts),
                  DBG_BODY);
        if (shape->cover) {
            fill_poly(fb, pts, outline_px(v, shape->cover, shape->n_cover < 48 ? shape->n_cover : 48, right, pts),
                      DBG_EDGE);
        }
    }
    for (int i = 0; whole && i < shape->n_extras; i++) {
        const struct fx_part *p = &shape->extras[i];
        for (int right = 0; right < ((p->kind & FX_PART_BOTH) ? 2 : 1); right++) {
            part_quad(v, p, right, 0, pts);
            fill_poly(fb, pts, 4, (p->kind & FX_PART_KIND) == FX_PART_SCREEN ? DBG_BG : DBG_EDGE);
            if ((p->kind & FX_PART_KIND) == FX_PART_SCREEN) {
                part_quad(v, p, right, 1.2f, pts);
                fill_poly(fb, pts, 4, DBG_LABEL);
            }
        }
    }
    const struct grid g = shape_grid();
    for (int key = 0; key < shape->n_keys && key < (int)sizeof(debug_heat); key++) {
        const struct fx_part *p = &shape->keys[key];
        const bool right = p->kind & FX_PART_RIGHT;
        const uint8_t down = half_down(d, right);
        const uint8_t color = down ? down : key_color(d, key);
        const bool held = (d->held >> key) & 1;
        const int encoder = key_encoder(key);
        const int kind = p->kind & FX_PART_KIND;
        int x, y, wide, high;
        part_rect(v, kind == FX_PART_KEY ? &g : NULL, p, &x, &y, &wide, &high);
        if (kind == FX_PART_DIAL && down) {
            knob(fb, x, y, wide, wide, DBG_BG, down, down, encoder >= 0 ? knob_shown[encoder] : 0);
            continue;
        }
        if (kind == FX_PART_DIAL) {
            const int heat = encoder >= 0 && knob_heat[encoder] > debug_heat[key] ? knob_heat[encoder] : debug_heat[key];
            knob(fb, x, y, wide, wide,
                 held || debug_heat[key] ? color : DBG_BG, held ? color : heat ? DBG_HEAT + heat : DBG_DIM,
                 held ? DBG_BG : encoder >= 0 && knob_heat[encoder] ? DBG_BRIGHT : DBG_LABEL,
                 encoder >= 0 ? knob_shown[encoder] : 0);
            continue;
        }
        if (p->rot == 0) {
            /* square to the screen: whole pixels on the grid, so every key is
             * the same size and the same distance from the next */
            fill(fb, x, y, wide, high, color);
        } else {
            part_quad(v, p, right, 0.5f, pts);
            fill_poly(fb, pts, 4, color);
        }
        if (kind == FX_PART_ROLLER) {
            /* ribs down it, rolling sideways as it turns, as it does under a thumb */
            const float turned = encoder >= 0 ? knob_shown[encoder] : 0;
            /* a rib every 3 pixels, stepping one along for each 10 degrees
             * the mark turns: three steps to come round, so the way it rolls shows */
            const int step = (int)floorf(turned / 10); /* the same way on both halves, as on the board */
            const int phase = (step % 3 + 3) % 3;
            for (int rib = phase; rib < wide; rib += 3) {
                line(fb, x + rib, y + 1, x + rib, y + high - 2,
                     encoder >= 0 && knob_heat[encoder] ? DBG_BRIGHT : DBG_BG);
            }
        }
    }
    board_fade(in);
}

/* --- typist: the typing speed, and the board as it is -------------------------- */

/*
 * The board drawn as it is (boards.h, as the README's pictures): its PCB,
 * the cover over its controller, its screen, and each key where it is,
 * coloured as `board` has them; encoders as knobs, a roller's ribs rolling
 * as it turns. Over it the typing speed as lights and big; along the foot
 * each half's battery and signal, and the output.
 *
 * It takes the whole screen, so the status text isn't drawn over it.
 */

#define SIGNAL_W 40 /* Status's link blocks */
#define KEYS_SPLIT 6 /* pixels between the halves */
#define TYPIST_LIGHTS 15
#define TYPIST_LIGHT_WPM 8 /* each light: the 15th is lit at 120 */
#define TYPIST_LIGHTS_Y 3
#define TYPIST_WPM_Y 12
#define TYPIST_BOARD_TOP 37
#define TYPIST_FOOT_Y 102

static float keys_time; /* 30ths of a second */

static void keys_start(uint16_t *palette) {
    debug_start(palette);
    keys_time = 0;
}

static int keys_block_x(int place) {
    return (FX_W - SIGNAL_W) / 2 + (place - 1) * (SIGNAL_W + 2);
}

static void put_big(uint8_t *fb, int x, int y, const char *s, uint8_t color, int scale);

/* A change of Bluetooth profile, for the screens that show the output as
 * text: for PROFILE_SHOWN 30ths of a second after it, the new profile
 * flashes in the output's place, even over USB. Timed in fx_frame. */
#define PROFILE_SHOWN 90
#define PROFILE_FLASH 15 /* on two thirds of this, off the rest */
static int profile_seen = -1;
static float profile_since = PROFILE_SHOWN;

static void profile_track(const struct fx_input *in) {
    if (in->status == NULL) {
        return;
    }
    if (profile_seen >= 0 && in->status->profile != profile_seen) {
        profile_since = 0;
    } else if (profile_since < PROFILE_SHOWN) {
        profile_since += in->step;
    }
    profile_seen = in->status->profile;
}

/* Whether the profile has just been picked, and so flashes where the
 * output is shown (even over USB); `off` in the off part of the flash. */
static bool profile_flash(const struct fx_input *in, bool *off) {
    if (in->status == NULL || profile_since >= PROFILE_SHOWN) {
        return false;
    }
    *off = (int)profile_since % PROFILE_FLASH >= PROFILE_FLASH * 2 / 3;
    return true;
}

/* Along the foot: each half's level, big, in its corner (a bolt over its %
 * while on USB power) with its signal's bars beside it, the output small
 * between them. */
static void typist_foot(uint8_t *fb, const struct fx_input *in) {
    const struct fx_debug *d = in->debug;
    const int y = TYPIST_FOOT_Y, inset = fx_corner_inset(y, y + 13) + 1;
    char buf[8];
    for (int half = 0; half < 2; half++) {
        /* a half away says why in its corner, in place of its level */
        uint8_t away;
        const char *word = away_word(&d->half[half], &away);
        if (word) {
            const int wide = 5 * (int)strlen(word) - 1;
            put(fb, half ? FX_W - inset - wide : inset, y + 7, word, away);
            continue;
        }
        bool estimate;
        const uint8_t level = half_level(in, half, &estimate);
        const uint8_t color = level ? battery_color(level) : DBG_LABEL;
        if (level) {
            snprintf(buf, sizeof(buf), "%u", level);
        } else {
            strcpy(buf, "--");
        }
        const int wide = 10 * (int)strlen(buf) - 2 + (level ? 6 : 0);
        const int x = half ? FX_W - inset - wide : inset;
        if ((in->status && in->status->charging[half]) ||
            !fx_battery_flash_off(level, (uint32_t)(keys_time * 1000 / 30))) {
            put_big(fb, x, y, buf, color, 2);
            if (level) {
                put(fb, x + wide - 4, y + 7, "%", color);
            }
        }
        /* over the %: a bolt while on USB power (flashing until 100) */
        charging_bolt(fb, level ? x + wide - 4 : x + wide + 2, y, in->status, half);
        signal_bars(fb, half ? x - 4 - 7 : x + wide + 4, y + 7, &d->half[half]);
    }
    bool off;
    if (in->status && in->status->usb && !profile_flash(in, &off)) {
        put(fb, (FX_W - 14) / 2, y + 7, "USB", d->usb_asleep ? DBG_WARN : in->status->host_connected ? DBG_GOOD : DBG_BAD);
    } else {
        profile_mark(fb, in, (FX_W - PROFILE_MARK_W) / 2, y + 7);
    }
}

static void keys_frame(uint8_t *fb, const struct fx_input *in) {
    memset(fb, DBG_BG, PIXELS);
    if (in->debug == NULL) {
        return;
    }
    const struct fx_debug *d = in->debug;
    keys_time += in->step;
    /* the speed: as lights along the top, green then red then violet, a
     * light for each TYPIST_LIGHT_WPM, and big under them */
    const int lights_x = (FX_W - (TYPIST_LIGHTS * 8 - 2)) / 2;
    for (int i = 0; i < TYPIST_LIGHTS; i++) {
        const uint8_t on = i < 5 ? DBG_GOOD : i < 10 ? DBG_BAD : DBG_LAYER;
        fill(fb, lights_x + i * 8, TYPIST_LIGHTS_Y, 6, 5, d->wpm >= (i + 1) * TYPIST_LIGHT_WPM ? on : DBG_DIM);
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", d->wpm > 999 ? 999 : d->wpm);
    /* with its unit beside it, the two together in the middle */
    const int wpm_w = 15 * (int)strlen(buf) - 3, wpm_x = (FX_W - (wpm_w + 3 + 14)) / 2;
    put_big(fb, wpm_x, TYPIST_WPM_Y, buf, d->wpm ? DBG_BRIGHT : DBG_LABEL, 3);
    put(fb, wpm_x + wpm_w + 3, TYPIST_WPM_Y + 14, "WPM", DBG_LABEL);
    const struct view v =
        shape_view(0, TYPIST_BOARD_TOP, FX_W, TYPIST_FOOT_Y - 4 - TYPIST_BOARD_TOP, KEYS_SPLIT, true);
    shape_board(fb, in, &v, true);
    typist_foot(fb, in);
}

/* --- status: halfway between Typist and Debug ---------------------------------- */

/*
 * The readings worth glancing at, set out like a racing car's steering
 * wheel display: bars and graphs rather than columns of numbers, and the
 * board big in the middle. Top to bottom:
 *
 *   each link's latest dBm, the halves' over their batteries and the
 *   computer's between after the profile in use, its waves and number
 *   (over USB, USB in its place, and no strip); under each a strip of
 *   its last FX_CONN_HISTORY slots of FX_CONN_SLOT_S seconds: a dim line
 *   while all was well, with nothing else coloured so one bad slot stands
 *   out: a short amber line for a weak signal, a taller red one for a red
 *   signal, a full-height red one where the link was down; a gap where the
 *   keyboard was idle (so the halves may have been asleep);
 *
 *   the board's keys as they are, as big as they go;
 *
 *   what has gone wrong, if anything, over the right battery;
 *
 *   each half's battery at its side: its level, big, and beside it its
 *   volts and average draw, their units between the halves; under them a
 *   bar.
 *
 * It takes the whole screen, so the status text isn't drawn over it.
 */

#define STATUS_SIGNAL_Y 4 /* each link's dBm */
#define STATUS_CONN_Y 13  /* and under it its connection strip, this high: */
#define STATUS_CONN_H 7    /* a minute the link was down; */
#define STATUS_CONN_LOW 5  /* one with a red signal; */
#define STATUS_CONN_WEAK 3 /* and one with an amber */
#define STATUS_KEYS_TOP 22 /* the board's keys */
#define STATUS_KEYS_H 64
#define STATUS_BATTERY_Y 91
#define STATUS_BATTERY_W 61
#define STATUS_CHARGE_STEP 18 /* 30ths of a second: each step of a charging half's bar filling */
#define STATUS_CHARGE_REST 3  /* steps' worth it rests full before starting again */
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
 * at its own side (while charging, the estimate), a bar
 * (filling while it charges), and its latest volts and draw. */
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
    }
    /* while charging, the steps above the level fill one by one, unhurried,
     * rest a moment full, and start again */
    static float charge_clock[2];
    const int lit = !level ? 0 : level < 15 ? 1 : (level + 5) / 10;
    int filling = 0;
    if (charging && level && lit < 10) {
        const int stops = 10 - lit + 1 + STATUS_CHARGE_REST;
        charge_clock[half] = fmodf(charge_clock[half] + in->step, (float)(stops * STATUS_CHARGE_STEP));
        filling = (int)(charge_clock[half] / STATUS_CHARGE_STEP);
        filling = filling > 10 - lit ? 10 - lit : filling;
    } else {
        charge_clock[half] = 0;
    }
    for (int step = 0; step < 10; step++) {
        const bool on = step < lit + filling;
        fill(fb, (half ? x + 2 : x) + step * 6, y + 18, 5, 3,
             !on ? DBG_DIM : level <= FX_WARN_BATTERY ? battery_color(level) : DBG_GOOD);
    }

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
        put(fb, half ? mid : mid - 5 * (int)strlen(buf), y + 8, "<", DBG_WARN); /* in the space before it */
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

    /* the keys as they are, as big as they go */
    const struct view v = shape_view(1, STATUS_KEYS_TOP, FX_W - 2, STATUS_KEYS_H, KEYS_SPLIT, false);
    shape_board(fb, in, &v, false);

    /* each link's latest dBm, and under it its connection strip; in the
     * middle the profile in use before the computer's, or USB (the profile
     * flashing there for a moment after a switch) */
    bool off;
    const bool flashing = profile_flash(in, &off);
    const struct fx_link *links[3] = {&d->half[0], &d->half[1], &d->host};
    for (int link = 0; link < 3; link++) {
        const int mid = keys_block_x(link == 2 ? 1 : link * 2) + SIGNAL_W / 2;
        if (link == 2 && usb_out) {
            if (flashing) {
                profile_mark(fb, in, mid - PROFILE_MARK_W / 2, STATUS_SIGNAL_Y);
            } else {
                put(fb, mid - 7, STATUS_SIGNAL_Y, "USB",
                    d->usb_asleep ? DBG_WARN : in->status->host_connected ? DBG_GOOD : DBG_BAD);
            }
        } else if (link == 2) {
            /* the mark, then the dBm without its unit (14 across) */
            const int left = mid - (PROFILE_MARK_W + 4 + 14) / 2;
            profile_mark(fb, in, left, STATUS_SIGNAL_Y);
            if (!links[link]->up) {
                put(fb, left + PROFILE_MARK_W + 4, STATUS_SIGNAL_Y, "--", DBG_BAD);
            } else if (links[link]->rssi_known) {
                snprintf(buf, sizeof(buf), "%d", links[link]->rssi < -99 ? -99 : links[link]->rssi);
                put(fb, left + PROFILE_MARK_W + 4, STATUS_SIGNAL_Y, buf, signal_color(links[link]));
            }
        } else if (!links[link]->up) {
            uint8_t away;
            const char *word = away_word(links[link], &away);
            put(fb, word ? mid - (5 * (int)strlen(word) - 1) / 2 : mid - 5, STATUS_SIGNAL_Y, word ? word : "--",
                word ? away : DBG_BAD);
        } else if (links[link]->rssi_known) {
            snprintf(buf, sizeof(buf), "%d", links[link]->rssi < -99 ? -99 : links[link]->rssi);
            const int left = mid - (5 * (int)strlen(buf) + 14) / 2;
            put(fb, left, STATUS_SIGNAL_Y, buf, signal_color(links[link]));
            put(fb, left + 5 * (int)strlen(buf), STATUS_SIGNAL_Y, "dBm", DBG_LABEL);
        }
    }
    /* not the computer's over USB: it would only ever be green */
    static const int place[3] = {0, 2, 1};
    for (int link = 0; link < (usb_out ? 2 : 3); link++) {
        const int x = keys_block_x(place[link]);
        const int foot = STATUS_CONN_Y + STATUS_CONN_H - 1;
        for (int px = 0; px < FX_CONN_HISTORY && px < SIGNAL_W; px++) {
            const int v = d->conn_log[link][FX_CONN_HISTORY - SIGNAL_W + px];
            if (v == 0 || v == FX_CONN_IDLE) {
                continue; /* nothing known, or idle: a gap */
            }
            if (v == FX_CONN_USB || (v != FX_SIGNAL_DOWN && v > SIGNAL_GOOD_DBM)) {
                plot(fb, x + px, foot, DBG_DIM);
                continue;
            }
            /* trouble stands up from the line, taller the worse it was */
            const int high = v == FX_SIGNAL_DOWN ? STATUS_CONN_H : v > SIGNAL_WARN_DBM ? STATUS_CONN_WEAK : STATUS_CONN_LOW;
            line(fb, x + px, foot - high + 1, x + px, foot, v > SIGNAL_WARN_DBM ? DBG_WARN : DBG_BAD);
        }
    }

    /* each half's dropped links over its battery, at its own side, as low
     * as the keys leave room for */
    for (int half = 0; half < 2; half++) {
        const int drops = d->half[half].drops;
        if (!drops) {
            continue;
        }
        snprintf(buf, sizeof(buf), "DR%d", drops > 99 ? 99 : drops);
        const int wide = 5 * (int)strlen(buf) - 1, x0 = half ? FX_W - 4 - wide : 4, x1 = x0 + wide - 1;
        int y = STATUS_BATTERY_Y - 9;
        for (int top = y; top >= STATUS_KEYS_TOP; top--) {
            bool clear = true;
            for (int row = top - 1; clear && row <= top + 7; row++) {
                for (int x = x0 - 1; clear && x <= x1 + 1; x++) {
                    clear = fb[row * FX_W + x] == DBG_BG;
                }
            }
            if (clear) {
                y = top;
                break;
            }
        }
        put(fb, x0, y, buf, DBG_WARN);
    }

    /* what else has gone wrong, if anything, worst first: a restart that
     * hung or crashed, a half's charging state that failed, USB reports the
     * computer didn't take, the computer's link dropping, the dongle
     * running hot. */
    struct {
        char text[6];
        uint8_t color;
    } trouble[5];
    int n = 0;
    if (d->crash == FX_CRASH_HANG || d->crash == FX_CRASH_FAULT) {
        strcpy(trouble[n].text, d->crash == FX_CRASH_HANG ? "HANG" : "CRSH");
        trouble[n++].color = DBG_BAD;
    }
    if (d->half_reads[0][0] == 'E' || d->half_reads[0][0] == 'T' || d->half_reads[1][0] == 'E' ||
        d->half_reads[1][0] == 'T') {
        strcpy(trouble[n].text, "CH");
        trouble[n++].color = DBG_BAD;
    }
    if (d->usb_errors) {
        snprintf(trouble[n].text, sizeof(trouble[n].text), "UE%u", d->usb_errors > 99 ? 99 : d->usb_errors);
        trouble[n++].color = DBG_WARN;
    }
    if (d->host.drops) {
        snprintf(trouble[n].text, sizeof(trouble[n].text), "DR%d", d->host.drops > 99 ? 99 : d->host.drops);
        trouble[n++].color = DBG_WARN;
    }
    if (d->dongle_temp_known && d->dongle_temp_c >= 60) {
        snprintf(trouble[n].text, sizeof(trouble[n].text), "%dC", d->dongle_temp_c > 99 ? 99 : d->dongle_temp_c);
        trouble[n++].color = DBG_WARN;
    }
    /* one a line, worst at the top, in the gap between the halves under
     * the middle's connection strip, each as high as the keys leave room */
    for (int i = 0, y = STATUS_CONN_Y + STATUS_CONN_H + 3; i < n; i++) {
        const int wide = 5 * (int)strlen(trouble[i].text) - 1, x0 = FX_W / 2 - wide / 2, x1 = x0 + wide - 1;
        for (; y <= STATUS_BATTERY_Y - 9; y++) {
            bool clear = true;
            for (int row = y - 1; clear && row <= y + 7; row++) {
                for (int x = x0 - 2; clear && x <= x1 + 2; x++) {
                    clear = fb[row * FX_W + x] == DBG_BG;
                }
            }
            if (clear) {
                break;
            }
        }
        if (y > STATUS_BATTERY_Y - 9) {
            break; /* no room for the rest */
        }
        put(fb, x0, y, trouble[i].text, trouble[i].color);
        y += 8;
    }

    /* each half's battery */
    status_half(fb, in, 0, 4, STATUS_BATTERY_Y);
    status_half(fb, in, 1, FX_W - 4 - STATUS_BATTERY_W, STATUS_BATTERY_Y);
    put(fb, FX_W / 2 - 2, STATUS_BATTERY_Y, "V", DBG_LABEL);
    put(fb, FX_W / 2 - 4, STATUS_BATTERY_Y + 8, "mW", DBG_LABEL);
    /* one bolt between the bars, under the mW, while either half is on USB
     * power (its bar filling says which) */
    for (int half = 0; half < 2; half++) {
        if (in->status && in->status->charging[half] && !in->status->bolt_off[half]) {
            put(fb, FX_W / 2 - 3, STATUS_BATTERY_Y + 16, FX_BOLT, DBG_TEXT);
            break;
        }
    }
}

/* --- showroom: the board in 3D, cel shaded, filmed like a car advert --------- */

/*
 * The board (`shape`, boards.h) built as solids: each half's PCB raised
 * from its outline, the cover over its controller, a keycap on a switch at
 * each key, its dials and rollers; and the dongle behind it. Each face is
 * flat, in one of a few bands of its colour by how the light falls on it,
 * and each solid has a black line round it. There is no depth buffer: the
 * solids are drawn back to front, which their being apart on the desk makes
 * easy, and the canvas holds which solid each pixel shows, for the lines.
 *
 * Quarter millimetres, as in boards.h: x across (the left half's inner edge
 * at -show_gap), y away from you (boards.h's y, which comes towards you,
 * the other way round), z up from the desk.
 */

#define SHOW_BG 1 /* the backdrop: SHOW_BG_N levels, dark to the spotlight */
#define SHOW_BG_N 32
#define SHOW_SHADOWED (SHOW_BG + SHOW_BG_N) /* the same in shadow */
#define SHOW_MAT (SHOW_SHADOWED + SHOW_BG_N)  /* the materials, SHOW_BANDS each */
#define SHOW_BANDS 5
enum { BAND_DARK, BAND_MID, BAND_LIT, BAND_RIM, BAND_GLINT };
enum { MAT_CASE, MAT_COVER, MAT_CAP, MAT_EXTRA, MAT_SCREEN, MAT_RUBBER, MAT_GLASS, MAT_DONGLE, MAT_HOT, SHOW_MATS = MAT_HOT + 3 };
#define SHOW_INK (SHOW_MAT + SHOW_MATS * SHOW_BANDS)
#define SHOW_PICTURE (SHOW_INK + 1) /* on the way in: what was showing, on the tiles it breaks into */
#define SHOW_CAPTION (SHOW_PICTURE + 1) /* SHOW_CAPTION_N levels, faded in */
#define SHOW_CAPTION_N 8

#define SHOW_PCB 28    /* the case's height */
#define SHOW_COVER 10  /* the cover over the controller, above that: as high as the keycaps' feet */
#define SHOW_SWITCH 10 /* switch showing under a keycap */
#define SHOW_CAP 22    /* a keycap's height */
#define SHOW_PRESS 8   /* how far a key goes down */
#define SHOW_NEAR 6.0f /* the nearest anything is drawn to the camera */
#define SHOW_BAR 9     /* the letterbox's bars, rows each */
#define SHOW_SHOT 240  /* a shot's length, 30ths of a second */
#define SHOW_PTS 40    /* the most points a face has (boards.h's outlines have up to 39) */

/* The dongle, behind the board: a wedge with its screen on the slope. */
#define DONGLE_FRONT 150.0f /* y of its front foot */
#define DONGLE_DEEP 150.0f
#define DONGLE_WIDE 200.0f
#define DONGLE_HIGH 170.0f
#define DONGLE_LEAN 46.0f /* how far back the top of its front is */
#define DONGLE_SCREEN_W 150.0f /* and its screen, the panel's shape */
#define DONGLE_SCREEN_H (DONGLE_SCREEN_W * FX_H / FX_W)

static struct {
    float pos[3], r[3], u[3], f[3]; /* where it is; right, up and ahead of it */
    float focal;                    /* pixels from the middle at 45 degrees */
} cam;

static float show_time; /* into the shots, 30ths of a second */
static float show_intro; /* 0..1 through the way in, while it is on */
static bool show_intro_now; /* fx_transition_step said so this frame */
static float show_bw, show_bd, show_gap; /* a half's width and depth; half the gap between them */
static float show_light[3], show_back[3]; /* to the key light, to the one behind */
static float show_glint;                  /* where the glint's band is across the screen */
static uint8_t show_id;                   /* the last solid's number in the canvas (it stops at 255) */
static uint8_t show_heat[64];             /* how lately each key was pressed */
static int show_keys, show_dials, show_rollers;
static float show_aim[3]; /* the detail shot's subject */

/*
 * The way in: what was showing, as a wall of tiles just in front of the
 * camera, so that at first it is the picture exactly. A sweep across it
 * knocks each tile loose: it tips forward, turns over and falls, a slab
 * with the picture on its face, and the board is behind. Tiles still in
 * the wall are one solid (no lines between them); each loose one is its
 * own, and fx_transition_row finds the picture on it ray by ray, from
 * where it is this frame (its slot: the order it was drawn in).
 */
#define TILE_COLS 10
#define TILE_ROWS 8
#define TILE_W (FX_W / TILE_COLS)
#define TILE_H (FX_H / TILE_ROWS)
#define TILES (TILE_COLS * TILE_ROWS)
#define TILE_DEPTH 0.32f   /* the wall's distance, of the camera's from its target */
#define TILE_THICK 8.0f    /* quarter mm */
#define TILE_FALL 300.0f   /* gravity, quarter mm a second squared: slow motion */
static float wall_pos[3], wall_r[3], wall_u[3], wall_f[3], wall_focal, wall_d; /* the camera the wall was built for */
static uint8_t wall_id, loose_from_id, n_loose;
static struct wall_tile {
    float o[3], x[3], y[3]; /* in the camera's space: its top left corner, along its top and down its side */
    uint8_t tile, shade;          /* which, and how lit (256ths, less one) */
} loose[TILES], wall_now; /* wall_now: the wall still standing, as one tile the size of the picture */

/* The points of the face being drawn, in the camera's space: static, as
 * the dongle's display thread has a small stack, and shared, as RAM is short. */
static float show_cam_pts[SHOW_PTS][3];

static inline void show_next(void) { show_id += show_id < 255; }

static inline float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static inline void cross3(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline void norm3(float *v) {
    const float l = sqrtf(dot3(v, v));
    if (l > 0) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

static inline float show_ease(float t) {
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static void to_cam(const float *p, float *out) {
    const float d[3] = {p[0] - cam.pos[0], p[1] - cam.pos[1], p[2] - cam.pos[2]};
    out[0] = dot3(d, cam.r);
    out[1] = dot3(d, cam.u);
    out[2] = dot3(d, cam.f);
}

/* The camera `dist` from `target`, round from the front by `yaw` degrees
 * (to the right) and up by `pitch`. */
static void show_camera(const float *target, float yaw, float pitch, float dist, float focal) {
    const float y = yaw * (TWO_PI / 360), p = pitch * (TWO_PI / 360), up[3] = {0, 0, 1};
    cam.pos[0] = target[0] + dist * sinf(y) * cosf(p);
    cam.pos[1] = target[1] - dist * cosf(y) * cosf(p);
    cam.pos[2] = target[2] + dist * sinf(p);
    for (int i = 0; i < 3; i++) {
        cam.f[i] = target[i] - cam.pos[i];
    }
    norm3(cam.f);
    cross3(cam.f, up, cam.r);
    norm3(cam.r);
    cross3(cam.r, cam.f, cam.u);
    cam.focal = focal;
}

/* A point of the board: boards.h's (x, y) on the left half (`side` 1) or
 * mirrored on to the right (-1), `z` up. */
static void show_point(float *out, float x, float y, float z, int side) {
    out[0] = side * (x - show_gap);
    out[1] = -y;
    out[2] = z;
}

/* A point of part `p`, (u, v) from its middle along its sides before it is
 * turned. */
static void part_point(float *out, const struct fx_part *p, float u, float v, float z) {
    const float a = p->rot * (TWO_PI / 360), c = p->rot ? cosf(a) : 1, s = p->rot ? sinf(a) : 0;
    show_point(out, p->x + u * c - v * s, p->y + u * s + v * c, z, p->kind & FX_PART_RIGHT ? -1 : 1);
}

enum { FILL_SOLID, FILL_GLINT, FILL_FLOOR_SHADOW, FILL_TOP_SHADOW };

/* Fill the face whose points, in the camera's space, are `pts`: in `color`,
 * marking the canvas with show_id; or as `mode` says. */
static void show_fill(uint8_t *fb, float (*pts)[3], int n, uint8_t color, uint8_t mode, uint8_t from_id) {
    /* off the near side (static: there is one display thread, and its stack is small) */
    static float clipped[SHOW_PTS + 4][3], xy[SHOW_PTS + 4][2];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const float *a = pts[i], *b = pts[(i + 1) % n];
        const bool in_a = a[2] >= SHOW_NEAR, in_b = b[2] >= SHOW_NEAR;
        if (in_a && m < SHOW_PTS + 4) {
            memcpy(clipped[m++], a, sizeof(clipped[0]));
        }
        if (in_a != in_b && m < SHOW_PTS + 4) {
            const float k = (SHOW_NEAR - a[2]) / (b[2] - a[2]);
            for (int c = 0; c < 3; c++) {
                clipped[m][c] = a[c] + (b[c] - a[c]) * k;
            }
            m++;
        }
    }
    if (m < 3) {
        return;
    }
    float top = FX_H, bottom = -1;
    for (int i = 0; i < m; i++) {
        xy[i][0] = FX_W / 2 + cam.focal * clipped[i][0] / clipped[i][2];
        xy[i][1] = FX_H / 2 - cam.focal * clipped[i][1] / clipped[i][2];
        top = xy[i][1] < top ? xy[i][1] : top;
        bottom = xy[i][1] > bottom ? xy[i][1] : bottom;
    }
    const uint8_t glint = color + (BAND_GLINT - (color - SHOW_MAT) % SHOW_BANDS);
    for (int y = top < 0 ? 0 : (int)top; y < FX_H && y <= (int)bottom; y++) {
        const float yc = y + 0.5f;
        float xs[32];
        int k = 0;
        for (int i = 0; i < m && k < 32; i++) {
            const float *a = xy[i], *b = xy[(i + 1) % m];
            if ((a[1] <= yc) != (b[1] <= yc)) {
                const float x = a[0] + (yc - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
                int j = k++;
                for (; j > 0 && xs[j - 1] > x; j--) {
                    xs[j] = xs[j - 1];
                }
                xs[j] = x;
            }
        }
        for (int c = 0; c + 1 < k; c += 2) {
            int x0 = (int)ceilf(xs[c] - 0.5f), x1 = (int)floorf(xs[c + 1] - 0.5f);
            x0 = x0 < 0 ? 0 : x0;
            x1 = x1 >= FX_W ? FX_W - 1 : x1;
            uint8_t *row = fb + y * FX_W, *ids = canvas + y * FX_W;
            for (int x = x0; x <= x1; x++) {
                switch (mode) {
                case FILL_FLOOR_SHADOW:
                    if (ids[x] == 0 && row[x] >= SHOW_BG && row[x] < SHOW_SHADOWED) {
                        row[x] += SHOW_BG_N;
                    }
                    break;
                case FILL_TOP_SHADOW:
                    if (ids[x] >= from_id && row[x] >= SHOW_MAT && row[x] < SHOW_MAT + 2 * SHOW_BANDS) {
                        row[x] -= (row[x] - SHOW_MAT) % SHOW_BANDS; /* the case or the cover, in shadow */
                    }
                    break;
                case FILL_GLINT: {
                    const float g = x + y * 0.5f - show_glint;
                    row[x] = g >= 0 && g < 5 ? glint : color;
                    ids[x] = show_id;
                    break;
                }
                default:
                    row[x] = color;
                    ids[x] = show_id;
                }
            }
        }
    }
}

/* Which band a face turned to `n` is in, seen from `view` (to the camera). */
static int show_band(const float *n, const float *view) {
    const float lit = dot3(n, show_light);
    if (dot3(n, show_back) > 0.4f && lit < 0.3f && dot3(n, view) < 0.5f) { /* caught side on */
        return BAND_RIM;
    }
    return lit > 0.62f ? BAND_LIT : lit > 0.2f ? BAND_MID : BAND_DARK;
}

/* A flat face (world points), if it faces the camera: `n` is its outward
 * normal. `mat` < 0: the dongle's screen. */
static void show_face(uint8_t *fb, const float (*pts)[3], int n, const float *normal, int mat, uint8_t mode) {
    const float to_cam_v[3] = {cam.pos[0] - pts[0][0], cam.pos[1] - pts[0][1], cam.pos[2] - pts[0][2]};
    if (dot3(normal, to_cam_v) <= 0) {
        return;
    }
    float(*c)[3] = show_cam_pts;
    n = n > SHOW_PTS ? SHOW_PTS : n;
    for (int i = 0; i < n; i++) {
        to_cam(pts[i], c[i]);
    }
    float view[3] = {to_cam_v[0], to_cam_v[1], to_cam_v[2]};
    norm3(view);
    const uint8_t color = mat < 0 ? SHOW_PICTURE : (uint8_t)(SHOW_MAT + mat * SHOW_BANDS + show_band(normal, view));
    show_fill(fb, c, n, color, mat < 0 ? FILL_SOLID : mode, 0);
}

/* A shadow (FILL_FLOOR_SHADOW on the desk, FILL_TOP_SHADOW on the solids
 * from `from_id` on) where the world points `pts` fall. */
static void show_shadow(uint8_t *fb, const float (*pts)[3], int n, uint8_t mode, uint8_t from_id) {
    float(*c)[3] = show_cam_pts;
    n = n > SHOW_PTS ? SHOW_PTS : n;
    for (int i = 0; i < n; i++) {
        to_cam(pts[i], c[i]);
    }
    show_fill(fb, c, n, 0, mode, from_id);
}

/* Where world point `p` falls on the desk in the key light. */
static void show_on_desk(const float *p, float *out) {
    out[0] = p[0] - show_light[0] * p[2] / show_light[2];
    out[1] = p[1] - show_light[1] * p[2] / show_light[2];
    out[2] = 0;
}

/* Raise boards.h's polygon `outline` (`n` points) from z0 to z1 on half
 * `side`: its walls, far to near, then its top. */
static void show_extrude(uint8_t *fb, const int16_t *outline, int n, float z0, float z1, int side, int mat) {
    static float top[SHOW_PTS][3], depth[SHOW_PTS];
    static uint8_t order[SHOW_PTS];
    float area = 0;
    int walls = 0;
    n = n > SHOW_PTS ? SHOW_PTS : n;
    for (int i = 0; i < n; i++) {
        show_point(top[i], outline[2 * i], outline[2 * i + 1], z1, side);
    }
    for (int i = 0; i < n; i++) {
        const float *a = top[i], *b = top[(i + 1) % n];
        area += a[0] * b[1] - b[0] * a[1];
    }
    for (int i = 0; i < n; i++) {
        const float *a = top[i], *b = top[(i + 1) % n];
        const float mid[3] = {(a[0] + b[0]) / 2 - cam.pos[0], (a[1] + b[1]) / 2 - cam.pos[1], (z0 + z1) / 2 - cam.pos[2]};
        float d = dot3(mid, cam.f);
        int j = walls++;
        for (; j > 0 && depth[j - 1] < d; j--) {
            depth[j] = depth[j - 1];
            order[j] = order[j - 1];
        }
        depth[j] = d;
        order[j] = (uint8_t)i;
    }
    show_next();
    for (int w = 0; w < walls; w++) {
        const float *a = top[order[w]], *b = top[(order[w] + 1) % n];
        float normal[3] = {b[1] - a[1], a[0] - b[0], 0};
        if (area < 0) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
        }
        norm3(normal);
        const float quad[4][3] = {{a[0], a[1], z0}, {b[0], b[1], z0}, {b[0], b[1], z1}, {a[0], a[1], z1}};
        show_face(fb, quad, 4, normal, mat, FILL_SOLID);
    }
    static const float up[3] = {0, 0, 1};
    show_next();
    show_face(fb, (const float(*)[3])top, n, up, mat, FILL_GLINT);
}

/* The face (a, b, c, ...) of a solid around `mid`: its normal, outward. */
static void show_normal(const float (*pts)[3], const float *mid, float *n) {
    const float e1[3] = {pts[1][0] - pts[0][0], pts[1][1] - pts[0][1], pts[1][2] - pts[0][2]};
    const float e2[3] = {pts[2][0] - pts[0][0], pts[2][1] - pts[0][1], pts[2][2] - pts[0][2]};
    cross3(e1, e2, n);
    const float out[3] = {pts[0][0] + pts[2][0] - 2 * mid[0], pts[0][1] + pts[2][1] - 2 * mid[1],
                          pts[0][2] + pts[2][2] - 2 * mid[2]};
    if (dot3(n, out) < 0) {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
    }
    norm3(n);
}

/* A block on part `p`: hw by hh at its foot (z0), `inset` less at its top
 * (z1); its sides, then (`lid`) its top. */
static void show_box(uint8_t *fb, const struct fx_part *p, float hw, float hh, float inset, float z0, float z1, int mat,
                     bool lid) {
    static const int8_t corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    float foot[4][3], top[4][3], mid[3];
    for (int i = 0; i < 4; i++) {
        part_point(foot[i], p, corner[i][0] * hw, corner[i][1] * hh, z0);
        part_point(top[i], p, corner[i][0] * (hw - inset), corner[i][1] * (hh - inset), z1);
    }
    part_point(mid, p, 0, 0, (z0 + z1) / 2);
    show_next();
    for (int i = 0; i < 4; i++) {
        const int j = (i + 1) % 4;
        float side[4][3], n[3];
        memcpy(side[0], foot[i], sizeof(side[0]));
        memcpy(side[1], foot[j], sizeof(side[0]));
        memcpy(side[2], top[j], sizeof(side[0]));
        memcpy(side[3], top[i], sizeof(side[0]));
        show_normal((const float(*)[3])side, mid, n);
        show_face(fb, (const float(*)[3])side, 4, n, mat, FILL_SOLID);
    }
    if (lid) { /* one solid with its sides: a line round each would crowd it */
        static const float up[3] = {0, 0, 1};
        show_face(fb, (const float(*)[3])top, 4, up, mat, FILL_GLINT);
    }
}

#define DIAL_SIDES 12

/* A dial: a short cylinder, a mark on its top. */
static void show_dial(uint8_t *fb, const struct fx_part *p, float r, float z0, float z1) {
    float foot[DIAL_SIDES][3], top[DIAL_SIDES][3], mid[3];
    for (int i = 0; i < DIAL_SIDES; i++) {
        const float a = (i + 0.5f) * (TWO_PI / DIAL_SIDES);
        part_point(foot[i], p, r * cosf(a), r * sinf(a), z0);
        part_point(top[i], p, 0.9f * r * cosf(a), 0.9f * r * sinf(a), z1);
    }
    part_point(mid, p, 0, 0, (z0 + z1) / 2);
    show_next();
    for (int i = 0; i < DIAL_SIDES; i++) {
        const int j = (i + 1) % DIAL_SIDES;
        float side[4][3], n[3];
        memcpy(side[0], foot[i], sizeof(side[0]));
        memcpy(side[1], foot[j], sizeof(side[0]));
        memcpy(side[2], top[j], sizeof(side[0]));
        memcpy(side[3], top[i], sizeof(side[0]));
        show_normal((const float(*)[3])side, mid, n);
        show_face(fb, (const float(*)[3])side, 4, n, MAT_EXTRA, FILL_SOLID);
    }
    static const float up[3] = {0, 0, 1};
    show_next();
    show_face(fb, (const float(*)[3])top, DIAL_SIDES, up, MAT_EXTRA, FILL_GLINT);
    float mark[4][3];
    part_point(mark[0], p, -2.5f, -0.25f * r, z1);
    part_point(mark[1], p, 2.5f, -0.25f * r, z1);
    part_point(mark[2], p, 2.5f, -0.8f * r, z1);
    part_point(mark[3], p, -2.5f, -0.8f * r, z1);
    show_face(fb, (const float(*)[3])mark, 4, up, MAT_CASE, FILL_SOLID);
}

#define ROLLER_STEPS 8

/* A roller: the top half of a wheel across the part, standing out of the case. */
static void show_roller(uint8_t *fb, const struct fx_part *p, float hw, float r) {
    float axis[3], ring[2][ROLLER_STEPS + 1][3];
    part_point(axis, p, 0, 0, SHOW_PCB);
    for (int k = 0; k <= ROLLER_STEPS; k++) {
        const float a = k * (TWO_PI / 2 / ROLLER_STEPS);
        part_point(ring[0][k], p, -hw, r * cosf(a), SHOW_PCB + r * sinf(a));
        part_point(ring[1][k], p, hw, r * cosf(a), SHOW_PCB + r * sinf(a));
    }
    show_next();
    for (int end = 0; end < 2; end++) {
        float n[3], at[3];
        part_point(at, p, end ? hw : -hw, 0, SHOW_PCB);
        for (int c = 0; c < 3; c++) {
            n[c] = at[c] - axis[c];
        }
        norm3(n);
        show_face(fb, (const float(*)[3])ring[end], ROLLER_STEPS + 1, n, MAT_RUBBER, FILL_SOLID);
    }
    for (int k = 0; k < ROLLER_STEPS; k++) {
        float strip[4][3], n[3];
        memcpy(strip[0], ring[0][k], sizeof(strip[0]));
        memcpy(strip[1], ring[1][k], sizeof(strip[0]));
        memcpy(strip[2], ring[1][k + 1], sizeof(strip[0]));
        memcpy(strip[3], ring[0][k + 1], sizeof(strip[0]));
        show_normal((const float(*)[3])strip, axis, n);
        /* ribs: every other strip dark */
        show_face(fb, (const float(*)[3])strip, 4, n, k & 1 ? MAT_RUBBER : MAT_EXTRA, FILL_SOLID);
    }
}

/* How far key `key` is down (quarter mm), and the colour it glows. */
static float show_key_state(const struct fx_input *in, int key, int *mat) {
    const uint8_t heat = key < 64 ? show_heat[key] : 0;
    const bool held = key < 64 && (in->held >> key & 1);
    *mat = heat > 170 || held ? MAT_HOT + 2 : heat > 100 ? MAT_HOT + 1 : heat > 30 ? MAT_HOT : *mat;
    return held || heat > 200 ? SHOW_PRESS : 0;
}

/* Part `p` (key position `key`, or -1 for an extra). */
static void show_part(uint8_t *fb, const struct fx_input *in, const struct fx_part *p, int key) {
    const float hw = p->w / 2.0f, hh = p->h / 2.0f;
    switch (p->kind & FX_PART_KIND) {
    case FX_PART_KEY: {
        int mat = p->kind & FX_PART_EXTRA ? MAT_EXTRA : MAT_CAP;
        const float press = show_key_state(in, key, &mat);
        const float foot = SHOW_PCB + SHOW_SWITCH - press;
        show_box(fb, p, hw - 14, hh - 14, 0, SHOW_PCB, foot, MAT_RUBBER, false);
        show_box(fb, p, hw - 4, hh - 4, 9, foot, foot + SHOW_CAP, mat, true);
        break;
    }
    case FX_PART_DIAL:
        show_dial(fb, p, hw - 4, SHOW_PCB, SHOW_PCB + 44);
        break;
    case FX_PART_ROLLER:
        show_roller(fb, p, hw - 3, hh);
        break;
    case FX_PART_SCREEN: {
        static const int8_t corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const float up[3] = {0, 0, 1};
        float quad[4][3];
        const float z = SHOW_PCB + (shape->cover ? SHOW_COVER : 0) + 0.5f;
        for (int i = 0; i < 4; i++) {
            part_point(quad[i], p, corner[i][0] * (hw - 2), corner[i][1] * (hh - 2), z);
        }
        show_next();
        show_face(fb, (const float(*)[3])quad, 4, up, MAT_GLASS, FILL_SOLID);
        for (int i = 0; i < 4; i++) {
            part_point(quad[i], p, corner[i][0] * (hw - 8), corner[i][1] * (hh - 8), z);
        }
        show_face(fb, (const float(*)[3])quad, 4, up, MAT_SCREEN, FILL_GLINT);
        break;
    }
    default:
        show_box(fb, p, hw, hh, 2, SHOW_PCB, SHOW_PCB + 12, MAT_RUBBER, true);
    }
}

/* Half `side` (1 left, -1 right): the case, the cover, then its parts far to near. */
static void show_half(uint8_t *fb, const struct fx_input *in, int side) {
    show_extrude(fb, shape->outline, shape->n_outline, 0, SHOW_PCB, side, MAT_CASE);
    const uint8_t top_id = show_id;
    if (shape->cover) {
        show_extrude(fb, shape->cover, shape->n_cover, SHOW_PCB, SHOW_PCB + SHOW_COVER, side, MAT_COVER);
    }

    struct fx_part parts[64];
    int16_t keys[64];
    float depth[64];
    int n = 0;
    for (int i = 0; i < shape->n_keys + shape->n_extras && n < 64; i++) {
        struct fx_part p = i < shape->n_keys ? shape->keys[i] : shape->extras[i - shape->n_keys];
        const bool right = p.kind & FX_PART_RIGHT;
        if (i >= shape->n_keys && side < 0 && (p.kind & FX_PART_BOTH)) {
            p.kind |= FX_PART_RIGHT; /* an extra on both halves: the right's */
        } else if (right != (side < 0)) {
            continue;
        }
        float mid[3], d[3];
        part_point(mid, &p, 0, 0, SHOW_PCB);
        for (int c = 0; c < 3; c++) {
            d[c] = mid[c] - cam.pos[c];
        }
        const float far = dot3(d, d);
        int j = n++;
        for (; j > 0 && depth[j - 1] < far; j--) {
            depth[j] = depth[j - 1];
            parts[j] = parts[j - 1];
            keys[j] = keys[j - 1];
        }
        depth[j] = far;
        parts[j] = p;
        keys[j] = (int16_t)(i < shape->n_keys ? i : -1);
    }

    /* each key's shadow on the case, then the parts */
    for (int i = 0; i < n; i++) {
        const struct fx_part *p = &parts[i];
        const int kind = p->kind & FX_PART_KIND;
        if (kind != FX_PART_KEY && kind != FX_PART_DIAL) {
            continue;
        }
        static const int8_t corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const float r = p->w / 2.0f - 4, lift = kind == FX_PART_KEY ? SHOW_SWITCH + SHOW_CAP * 0.6f : 30;
        float quad[4][3];
        for (int c = 0; c < 4; c++) {
            float at[3];
            part_point(at, p, corner[c][0] * r, corner[c][1] * (p->h / 2.0f - 4), SHOW_PCB);
            quad[c][0] = at[0] - show_light[0] * lift / show_light[2];
            quad[c][1] = at[1] - show_light[1] * lift / show_light[2];
            quad[c][2] = SHOW_PCB;
        }
        show_shadow(fb, (const float(*)[3])quad, 4, FILL_TOP_SHADOW, top_id);
    }
    for (int i = 0; i < n; i++) {
        show_part(fb, in, &parts[i], keys[i]);
    }
}

/* The dongle's screen: its middle, and up it (it leans back). */
static void dongle_screen(float *mid, float *up) {
    const float slope = sqrtf(DONGLE_LEAN * DONGLE_LEAN + DONGLE_HIGH * DONGLE_HIGH);
    up[0] = 0;
    up[1] = DONGLE_LEAN / slope;
    up[2] = DONGLE_HIGH / slope;
    /* a little in front of the face */
    mid[0] = 0;
    mid[1] = DONGLE_FRONT + 0.55f * DONGLE_LEAN - 0.5f * up[2];
    mid[2] = 0.55f * DONGLE_HIGH + 0.5f * up[1];
}

/* The dongle's corners: x -, + by its side view's front foot, back foot,
 * back top, front top. */
static void dongle_corners(float (*out)[3]) {
    const float side[4][2] = {{DONGLE_FRONT, 0},
                              {DONGLE_FRONT + DONGLE_DEEP, 0},
                              {DONGLE_FRONT + DONGLE_DEEP, DONGLE_HIGH},
                              {DONGLE_FRONT + DONGLE_LEAN, DONGLE_HIGH}};
    for (int i = 0; i < 8; i++) {
        out[i][0] = i < 4 ? -DONGLE_WIDE / 2 : DONGLE_WIDE / 2;
        out[i][1] = side[i % 4][0];
        out[i][2] = side[i % 4][1];
    }
}

/* The dongle: its case, and its screen. */
static void show_dongle(uint8_t *fb) {
    float c[8][3], mid[3] = {0, DONGLE_FRONT + DONGLE_DEEP / 2, DONGLE_HIGH / 2};
    dongle_corners(c);
    show_next();
    for (int s = 0; s < 2; s++) {
        const float n[3] = {s ? 1.0f : -1.0f, 0, 0};
        const float(*face)[3] = (const float(*)[3])c[s * 4];
        show_face(fb, face, 4, n, MAT_DONGLE, FILL_SOLID);
    }
    for (int i = 1; i < 4; i++) { /* not its foot */
        const int j = (i + 1) % 4;
        float face[4][3], n[3];
        memcpy(face[0], c[i], sizeof(face[0]));
        memcpy(face[1], c[4 + i], sizeof(face[0]));
        memcpy(face[2], c[4 + j], sizeof(face[0]));
        memcpy(face[3], c[j], sizeof(face[0]));
        show_normal((const float(*)[3])face, mid, n);
        show_face(fb, (const float(*)[3])face, 4, n, MAT_DONGLE, i == 2 ? FILL_GLINT : FILL_SOLID);
    }

    float at[3], up[3], screen[4][3];
    dongle_screen(at, up);
    const float n[3] = {0, -up[2], up[1]};
    for (int i = 0; i < 4; i++) {
        const float across = (i == 1 || i == 2 ? 0.5f : -0.5f) * DONGLE_SCREEN_W;
        const float high = (i < 2 ? 0.5f : -0.5f) * DONGLE_SCREEN_H;
        for (int k = 0; k < 3; k++) {
            screen[i][k] = at[k] + up[k] * high + (k == 0 ? across : 0);
        }
    }
    show_next();
    show_face(fb, (const float(*)[3])screen, 4, n, MAT_GLASS, FILL_GLINT);
}

/* The shadows on the desk: the dongle's, and each half's. */
static void show_desk_shadows(uint8_t *fb) {
    /* the dongle's: round its foot and where its top falls */
    float c[8][3], pts[16][3], hull[17][3];
    dongle_corners(c);
    for (int i = 0; i < 8; i++) {
        show_on_desk(c[i], pts[i]);
        memcpy(pts[8 + i], c[i], sizeof(pts[0]));
        pts[8 + i][2] = 0;
    }
    /* sorted by x then y, and wrapped (Andrew's monotone chain) */
    for (int i = 1; i < 16; i++) {
        float p[3];
        memcpy(p, pts[i], sizeof(p));
        int j = i;
        for (; j > 0 && (pts[j - 1][0] > p[0] || (pts[j - 1][0] == p[0] && pts[j - 1][1] > p[1])); j--) {
            memcpy(pts[j], pts[j - 1], sizeof(p));
        }
        memcpy(pts[j], p, sizeof(p));
    }
    int h = 0;
    for (int pass = 0; pass < 2; pass++) {
        const int start = h;
        for (int k = 0; k < 16; k++) {
            const float *p = pts[pass ? 15 - k : k];
            while (h >= start + 2 && (hull[h - 1][0] - hull[h - 2][0]) * (p[1] - hull[h - 2][1]) -
                                             (hull[h - 1][1] - hull[h - 2][1]) * (p[0] - hull[h - 2][0]) <= 0) {
                h--;
            }
            memcpy(hull[h++], p, sizeof(hull[0]));
        }
        h--;
    }
    show_shadow(fb, (const float(*)[3])hull, h, FILL_FLOOR_SHADOW, 0);

    /* a half's: its foot, and as far as its keycaps' shadows reach */
    const float lift = SHOW_PCB + SHOW_SWITCH + SHOW_CAP * 0.7f;
    for (int side = -1; side <= 1; side += 2) {
        static float outline[SHOW_PTS][3];
        const int n = shape->n_outline > SHOW_PTS ? SHOW_PTS : shape->n_outline;
        for (int i = 0; i < n; i++) {
            show_point(outline[i], shape->outline[2 * i], shape->outline[2 * i + 1], 0, side);
        }
        show_shadow(fb, (const float(*)[3])outline, n, FILL_FLOOR_SHADOW, 0);
        for (int i = 0; i < n; i++) {
            outline[i][0] -= show_light[0] * lift / show_light[2];
            outline[i][1] -= show_light[1] * lift / show_light[2];
        }
        show_shadow(fb, (const float(*)[3])outline, n, FILL_FLOOR_SHADOW, 0);
    }
}

/* The backdrop: dark, and lighter where the spotlight falls, dithered. */
static void show_backdrop(uint8_t *fb) {
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = 0; y < FX_H; y++) {
        const float dy = (y - FX_H * 0.62f) / (FX_H * 0.62f);
        for (int x = 0; x < FX_W; x++) {
            const float dx = (x - FX_W / 2) / (FX_W * 0.7f);
            float v = 1 - (dx * dx + dy * dy);
            v = v < 0 ? 0 : v * v;
            int level = (int)(v * (SHOW_BG_N - 1) + bayer[(y & 3) * 4 + (x & 3)] / 16.0f);
            fb[y * FX_W + x] = (uint8_t)(SHOW_BG + (level > SHOW_BG_N - 1 ? SHOW_BG_N - 1 : level));
        }
    }
}

static float hashf(int x, int y);

/* The wall goes up in front of the camera as it is now, `dist` from its target. */
static void wall_build(float dist) {
    memcpy(wall_pos, cam.pos, sizeof(wall_pos));
    memcpy(wall_r, cam.r, sizeof(wall_r));
    memcpy(wall_u, cam.u, sizeof(wall_u));
    memcpy(wall_f, cam.f, sizeof(wall_f));
    wall_focal = cam.focal;
    wall_d = TILE_DEPTH * dist;
}

/* When tile `i` comes loose, 0..1 through the way in: a sweep from the top
 * left, ragged. */
static float tile_loose_at(int i) {
    const float sweep = 0.6f * (i % TILE_COLS) / (TILE_COLS - 1) + 0.4f * (i / TILE_COLS) / (TILE_ROWS - 1);
    return 0.06f + 0.62f * (0.85f * sweep + 0.15f * hashf(i, 1));
}

/* Where tile `i` is `secs` after it came loose (up to 0: in the wall): its
 * middle, along its top and down its side (its whole width and height), and
 * its face's normal. */
static void tile_place(int i, float secs, float *mid, float *ax, float *ay, float *n) {
    const float k = wall_d / wall_focal;
    const float cx = ((i % TILE_COLS + 0.5f) * TILE_W - FX_W / 2) * k;
    const float cy = -((i / TILE_COLS + 0.5f) * TILE_H - FX_H / 2) * k;
    for (int c = 0; c < 3; c++) {
        mid[c] = wall_pos[c] + wall_r[c] * cx + wall_u[c] * cy + wall_f[c] * wall_d;
        ax[c] = wall_r[c] * TILE_W * k;
        ay[c] = -wall_u[c] * TILE_H * k;
        n[c] = -wall_f[c];
    }
    if (secs <= 0) {
        return;
    }
    /* thrown a little towards the camera and up, then falling; tipping
     * forward over its top, with some twist */
    const float toward = 50 + 90 * hashf(i, 2), aside = (hashf(i, 3) - 0.5f) * 70, up = 30 + 60 * hashf(i, 4);
    for (int c = 0; c < 3; c++) {
        mid[c] += (-wall_f[c] * toward + wall_r[c] * aside) * secs;
    }
    mid[2] += up * secs - 0.5f * TILE_FALL * secs * secs;
    float axis[3];
    for (int c = 0; c < 3; c++) {
        axis[c] = wall_r[c] + wall_u[c] * (hashf(i, 5) - 0.5f) * 0.9f + wall_f[c] * (hashf(i, 6) - 0.5f) * 0.9f;
    }
    norm3(axis);
    const float turn = (1.4f + 1.8f * hashf(i, 7)) * secs, co = cosf(turn), si = sinf(turn);
    float *const vs[3] = {ax, ay, n};
    for (int v = 0; v < 3; v++) { /* Rodrigues: about `axis` by `turn` */
        float x[3];
        const float along = dot3(axis, vs[v]);
        cross3(axis, vs[v], x);
        for (int c = 0; c < 3; c++) {
            vs[v][c] = vs[v][c] * co + x[c] * si + axis[c] * along * (1 - co);
        }
    }
}

/* A tile's face (`out` > 0) or back, its corners clockwise from top left. */
static void tile_face(const float *mid, const float *ax, const float *ay, const float *n, float out,
                      float (*face)[3]) {
    for (int c = 0; c < 3; c++) {
        const float at = mid[c] + n[c] * out;
        face[0][c] = at - ax[c] / 2 - ay[c] / 2;
        face[1][c] = at + ax[c] / 2 - ay[c] / 2;
        face[2][c] = at + ax[c] / 2 + ay[c] / 2;
        face[3][c] = at - ax[c] / 2 + ay[c] / 2;
    }
}

/* The wall, `t` through the way in: what is left of it, then each loose
 * tile, far to near. */
static void show_wall(uint8_t *fb, float t) {
    const float secs_in = FX_SAVER_IN_MS / 1000.0f;
    uint8_t order[TILES];
    float depth[TILES];
    int n = 0;

    show_next();
    wall_id = show_id;
    {
        float mid[3], ax[3], ay[3], nn[3], quad[4][3];
        const float *basis[3] = {cam.r, cam.u, cam.f};
        tile_place(0, 0, mid, ax, ay, nn);
        tile_face(mid, ax, ay, nn, 0, quad);
        to_cam(quad[0], wall_now.o);
        for (int c = 0; c < 3; c++) {
            wall_now.x[c] = dot3(ax, basis[c]) * TILE_COLS;
            wall_now.y[c] = dot3(ay, basis[c]) * TILE_ROWS;
        }
        wall_now.shade = 255;
        wall_now.tile = 0;
    }
    for (int i = 0; i < TILES; i++) {
        float mid[3], ax[3], ay[3], nn[3], quad[4][3];
        if (t > tile_loose_at(i)) {
            tile_place(i, (t - tile_loose_at(i)) * secs_in, mid, ax, ay, nn);
            const float d[3] = {mid[0] - cam.pos[0], mid[1] - cam.pos[1], mid[2] - cam.pos[2]};
            const float far = dot3(d, cam.f);
            int j = n++;
            for (; j > 0 && depth[j - 1] < far; j--) {
                depth[j] = depth[j - 1];
                order[j] = order[j - 1];
            }
            depth[j] = far;
            order[j] = (uint8_t)i;
            continue;
        }
        tile_place(i, 0, mid, ax, ay, nn);
        tile_face(mid, ax, ay, nn, 0, quad);
        show_face(fb, (const float(*)[3])quad, 4, nn, -1, FILL_SOLID);
    }

    n_loose = 0;
    loose_from_id = (uint8_t)(show_id + 1);
    for (int k = 0; k < n && show_id < 255; k++) {
        const int i = order[k];
        const float secs = (t - tile_loose_at(i)) * secs_in;
        float mid[3], ax[3], ay[3], nn[3], face[4][3], back[4][3];
        tile_place(i, secs, mid, ax, ay, nn);
        tile_face(mid, ax, ay, nn, TILE_THICK / 2, face);
        tile_face(mid, ax, ay, nn, -TILE_THICK / 2, back);
        show_next();
        const float behind[3] = {-nn[0], -nn[1], -nn[2]};
        show_face(fb, (const float(*)[3])back, 4, behind, MAT_DONGLE, FILL_SOLID);
        for (int e = 0; e < 4; e++) {
            float edge[4][3], en[3];
            memcpy(edge[0], face[e], sizeof(edge[0]));
            memcpy(edge[1], face[(e + 1) % 4], sizeof(edge[0]));
            memcpy(edge[2], back[(e + 1) % 4], sizeof(edge[0]));
            memcpy(edge[3], back[e], sizeof(edge[0]));
            show_normal((const float(*)[3])edge, mid, en);
            show_face(fb, (const float(*)[3])edge, 4, en, MAT_RUBBER, FILL_SOLID);
        }
        show_face(fb, (const float(*)[3])face, 4, nn, -1, FILL_SOLID);

        /* where the picture on it is, for fx_transition_row; and how lit,
         * taking the light's bands over its first moments loose */
        const float *basis[3] = {cam.r, cam.u, cam.f};
        to_cam(face[0], loose[n_loose].o);
        for (int c = 0; c < 3; c++) {
            loose[n_loose].x[c] = dot3(ax, basis[c]);
            loose[n_loose].y[c] = dot3(ay, basis[c]);
        }
        float view[3] = {cam.pos[0] - mid[0], cam.pos[1] - mid[1], cam.pos[2] - mid[2]};
        norm3(view);
        const int band = show_band(nn, view);
        const float lit = band == BAND_DARK ? 0.5f : band == BAND_MID ? 0.78f : 1;
        loose[n_loose].shade = (uint8_t)(255 * (1 - (1 - lit) * clampf(secs * 1.5f, 0, 1)));
        loose[n_loose].tile = (uint8_t)i;
        n_loose++;
    }
}

/* A line round each solid: on its pixels next to one drawn before it (or
 * to the backdrop). */
static void show_ink(uint8_t *fb) {
    for (int y = 0; y < FX_H; y++) {
        for (int x = 0; x < FX_W; x++) {
            const int i = y * FX_W + x;
            const uint8_t id = canvas[i];
            if (id && ((x > 0 && canvas[i - 1] < id) || (x < FX_W - 1 && canvas[i + 1] < id) ||
                       (y > 0 && canvas[i - FX_W] < id) || (y < FX_H - 1 && canvas[i + FX_W] < id))) {
                fb[i] = SHOW_INK;
            }
        }
    }
}

/*
 * The shots, cut from one to the next, each SHOW_SHOT long: the camera
 * moving from `from` to `to`, each the target (across in half-widths from
 * the middle, back from the board's front edge in its depths, up), then
 * yaw and pitch in degrees and the distance in half-widths. The last ends
 * where the first starts, and the way in ends there too.
 */
enum { SHOW_AT_BOARD, SHOW_AT_AIM };
enum { CAPTION_NONE, CAPTION_NAME, CAPTION_KEYS, CAPTION_ENCODERS };
static const struct {
    float from[6], to[6];
    float focal;
    uint8_t at, caption;
} show_shots[] = {
    /* the hero: all of it, turning slowly */
    {{0, 0.45f, 20, -28, 30, 2.6f}, {0, 0.45f, 20, -8, 24, 2.35f}, 150, SHOW_AT_BOARD, CAPTION_NAME},
    /* low along the left half's keys */
    {{-0.8f, 0.45f, 40, -62, 13, 1.15f}, {-0.3f, 0.55f, 40, -48, 17, 1.05f}, 120, SHOW_AT_BOARD, CAPTION_NONE},
    /* from above, turning */
    {{0, 0.5f, 0, 18, 64, 2.7f}, {0, 0.5f, 0, -14, 74, 2.4f}, 150, SHOW_AT_BOARD, CAPTION_KEYS},
    /* close on the right thumb's dial (or roller, or key) */
    {{0, 0, 10, 30, 36, 0.75f}, {0, 0, 10, 58, 28, 0.6f}, 120, SHOW_AT_AIM, CAPTION_ENCODERS},
    /* the right half, pulling back to all of it */
    {{0.55f, 0.4f, 30, 52, 44, 0.95f}, {0, 0.45f, 20, -28, 30, 2.6f}, 150, SHOW_AT_BOARD, CAPTION_NONE},
};
#define SHOW_SHOTS ((int)(sizeof(show_shots) / sizeof(show_shots[0])))

static char show_captions[4][24];

/* A shot's camera, `u` 0..1 through it: target, yaw, pitch, distance (quarter mm). */
static void shot_camera(int shot, float u, float *target, float *view) {
    const float *a = show_shots[shot].from, *b = show_shots[shot].to;
    const float half = show_bw + show_gap;
    float k[6];
    for (int i = 0; i < 6; i++) {
        k[i] = a[i] + (b[i] - a[i]) * u;
    }
    if (show_shots[shot].at == SHOW_AT_AIM) {
        target[0] = show_aim[0] + k[0];
        target[1] = show_aim[1] + k[1];
        target[2] = show_aim[2] + k[2];
    } else {
        target[0] = k[0] * half;
        target[1] = -show_bd + k[1] * show_bd;
        target[2] = k[2];
    }
    view[0] = k[3];
    view[1] = k[4];
    view[2] = k[5] * half;
}

static void showroom_start(uint16_t *palette) {
    for (int i = 0; i < SHOW_BG_N; i++) {
        const int k = i * 256 / (SHOW_BG_N - 1);
        palette[SHOW_BG + i] = color565(mix(showroom_backdrop[0], showroom_backdrop[1], k));
        palette[SHOW_SHADOWED + i] = color565(mix(showroom_shadow[0], showroom_shadow[1], k));
    }
    for (int m = 0; m < MAT_HOT; m++) {
        for (int b = 0; b < SHOW_BANDS; b++) {
            palette[SHOW_MAT + m * SHOW_BANDS + b] = color565(showroom_materials[m][b]);
        }
    }
    for (int h = 0; h < 3; h++) {
        for (int b = 0; b < SHOW_BANDS; b++) {
            const struct color glow = b == BAND_DARK ? mix(showroom_glow, showroom_ink, 90) : showroom_glow;
            palette[SHOW_MAT + (MAT_HOT + h) * SHOW_BANDS + b] =
                color565(mix(showroom_materials[MAT_CAP][b], b == BAND_GLINT ? (struct color){255, 255, 255} : glow,
                             (h + 1) * 80));
        }
    }
    palette[SHOW_INK] = color565(showroom_ink);
    palette[SHOW_PICTURE] = color565(showroom_materials[MAT_GLASS][BAND_MID]);
    for (int i = 0; i < SHOW_CAPTION_N; i++) {
        palette[SHOW_CAPTION + i] = color565(mix((struct color){0, 0, 0}, showroom_caption, (i + 1) * 256 / SHOW_CAPTION_N));
    }

    /* the board's size, what is on it, and the detail shot's subject */
    show_bw = 0;
    show_bd = 0;
    for (int i = 0; i < shape->n_outline; i++) {
        show_bw = -shape->outline[2 * i] > show_bw ? -shape->outline[2 * i] : show_bw;
        show_bd = shape->outline[2 * i + 1] > show_bd ? shape->outline[2 * i + 1] : show_bd;
    }
    show_gap = show_bw * 0.06f;
    show_keys = show_dials = show_rollers = 0;
    int best = -1, best_score = -1;
    for (int i = 0; i < shape->n_keys; i++) {
        const struct fx_part *p = &shape->keys[i];
        const int kind = p->kind & FX_PART_KIND;
        show_keys += kind == FX_PART_KEY;
        show_dials += kind == FX_PART_DIAL;
        show_rollers += kind == FX_PART_ROLLER;
        const int score = !(p->kind & FX_PART_RIGHT) ? -1
                          : kind == FX_PART_DIAL     ? 1000
                          : kind == FX_PART_ROLLER   ? 900
                                                     : (p->rot < 0 ? -p->rot : p->rot);
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best >= 0) {
        part_point(show_aim, &shape->keys[best], 0, 0, SHOW_PCB);
    }
    static const char *const names[FX_BOARDS] = {"WATERFOWL", "AURORA CORNE", "URCHIN", "FORAGER"};
    const int board = (int)(shape - fx_boards);
    snprintf(show_captions[CAPTION_NONE], sizeof(show_captions[0]), "%s", "");
    snprintf(show_captions[CAPTION_NAME], sizeof(show_captions[0]), "%s",
             board >= 0 && board < FX_BOARDS ? names[board] : "");
    snprintf(show_captions[CAPTION_KEYS], sizeof(show_captions[0]), "%d KEYS", show_keys);
    if (show_dials && show_rollers) {
        snprintf(show_captions[CAPTION_ENCODERS], sizeof(show_captions[0]), "%d DIALS  %d ROLLERS", show_dials,
                 show_rollers);
    } else if (show_dials || show_rollers) {
        snprintf(show_captions[CAPTION_ENCODERS], sizeof(show_captions[0]), "%d %s", show_dials + show_rollers,
                 show_dials ? (show_dials > 1 ? "DIALS" : "DIAL") : show_rollers > 1 ? "ROLLERS" : "ROLLER");
    } else {
        show_captions[CAPTION_ENCODERS][0] = 0;
    }

    const float light[3] = {-0.45f, -0.6f, 0.66f}, back[3] = {0.55f, 0.75f, 0.35f};
    memcpy(show_light, light, sizeof(light));
    memcpy(show_back, back, sizeof(back));
    norm3(show_light);
    norm3(show_back);
    memset(show_heat, 0, sizeof(show_heat));
    show_time = 0;
    show_glint = -100;
    show_intro_now = false;
    wall_d = 0;
}

static void showroom_frame(uint8_t *fb, const struct fx_input *in) {
    const bool intro = show_intro_now;
    show_intro_now = false;
    for (int i = 0; i < 64; i++) {
        const int heat = show_heat[i] - (int)(in->step * 9);
        show_heat[i] = (uint8_t)(heat < 0 ? 0 : heat);
    }
    for (int k = 0; k < in->n_keys; k++) {
        if (in->keys[k] < 64) {
            show_heat[in->keys[k]] = 255;
        }
    }

    /* where the camera is */
    float target[3], view[3], focal;
    int bar = SHOW_BAR, shot = 0;
    float u = 0;
    if (intro) {
        /* the first shot, from further back and round, closing in all the while */
        shot_camera(0, 0, target, view);
        view[2] *= 1 + 0.25f * (1 - show_ease(show_intro));
        view[0] -= 14 * (1 - show_ease(show_intro));
        focal = show_shots[0].focal;
        bar = (int)(SHOW_BAR * show_ease((show_intro - 0.15f) / 0.4f) + 0.5f);
        show_time = 0;
    } else {
        show_time += in->step * (1 + in->energy * 0.6f);
        shot = (int)(show_time / SHOW_SHOT) % SHOW_SHOTS;
        u = fmodf(show_time, SHOW_SHOT) / SHOW_SHOT;
        shot_camera(shot, 0.5f * u + 0.5f * show_ease(u), target, view);
        focal = show_shots[shot].focal;
    }
    show_camera(target, view[0], view[1], view[2], focal);
    show_glint += in->step * 1.3f;
    if (show_glint > 300) {
        show_glint = -80;
    }

    show_backdrop(fb);
    memset(canvas, 0, PIXELS);
    show_id = 0;
    show_desk_shadows(fb);
    show_dongle(fb);
    /* the half beyond the gap from the camera first */
    show_half(fb, in, cam.pos[0] > 0 ? 1 : -1);
    show_half(fb, in, cam.pos[0] > 0 ? -1 : 1);
    if (intro) {
        if (!wall_d) {
            wall_build(view[2]);
        }
        show_wall(fb, show_intro);
    }
    show_ink(fb);

    /* the letterbox, and a caption in it now and then */
    memset(fb, 0, (size_t)bar * FX_W);
    memset(fb + (FX_H - bar) * FX_W, 0, (size_t)bar * FX_W);
    const char *caption = intro ? "" : show_captions[show_shots[shot].caption];
    const int fade = (int)(show_ease((u - 0.08f) / 0.12f) * (1 - show_ease((u - 0.76f) / 0.12f)) * SHOW_CAPTION_N);
    if (*caption && fade > 0 && bar >= 8) {
        /* letter-spaced, unless that would run into the corners */
        const int len = (int)strlen(caption), pitch = len * 7 > 110 ? 6 : 7, left = (FX_W - (len * pitch - 2)) / 2;
        for (int i = 0; i < len; i++) {
            glyph(fb, left + i * pitch, FX_H - bar + (bar - 7) / 2, caption[i],
                  (uint8_t)(SHOW_CAPTION + (fade > SHOW_CAPTION_N ? SHOW_CAPTION_N : fade) - 1));
        }
    }
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
    {"TYPIST", keys_start, keys_frame, debug_colors, NULL, {0}, .keys = true},
    {"THERMAL", thermal_start, thermal_frame, NULL, &thermal_text, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"TRACKER", tracker_start, tracker_frame, tracker_colors, NULL, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"TERMINAL", term_start, term_frame, term_colors, NULL, {.font = FX_FONT_PIXEL, .drop = CORNER_DROP}},
    {"WARP", warp_start, warp_frame, NULL, &warp_text, {.dots = true, .drop = CORNER_DROP}, .saver = FX_TR_HYPERSPACE},
    {"CODE", code_start, code_frame, NULL, &code_text, {.font = FX_FONT_PIXEL, .dots = true, .drop = CORNER_DROP}, .saver = FX_TR_MELT},
    {"LIFE", life_start, life_frame, NULL, &life_text, {.dots = true, .drop = CORNER_DROP}, .saver = FX_TR_INFECT},
    {"SHOWROOM", showroom_start, showroom_frame, NULL, &showroom_text, {.dots = true, .drop = CORNER_DROP},
     .saver = FX_TR_COLLAPSE},
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
    profile_track(in);
    away_clock = fmodf(away_clock + in->step, AWAY_FLASH * 100);
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
    } else if (tr->kind == FX_TR_COLLAPSE) {
        show_intro = tr->t; /* Showroom films its way in itself */
        show_intro_now = true;
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

/*
 * Into Showroom. The animation draws the wall of tiles (show_wall) in
 * SHOW_PICTURE: on what is left of the wall, and on each loose tile, the
 * picture is found where the pixel's ray meets its face, shaded as it is
 * lit (the wall isn't).
 */
static void collapse_row(const struct fx_transition *tr, int y, uint16_t *out) {
    const uint8_t *fb = &tr->fb[y * FX_W], *ids = &canvas[y * FX_W];
    for (int x = 0; x < FX_W; x++) {
        const int slot = ids[x] - loose_from_id;
        const struct wall_tile *t = ids[x] == wall_id ? &wall_now : slot >= 0 && slot < n_loose ? &loose[slot] : NULL;
        if (fb[x] != SHOW_PICTURE || t == NULL) {
            out[x] = tr->palette[fb[x]];
        } else {
            const float *o = t->o, *ax = t->x, *ay = t->y;
            float n[3]; /* its face's normal, towards the camera */
            cross3(ax, ay, n);
            const float ray[3] = {(x + 0.5f - FX_W / 2) / cam.focal, -(y + 0.5f - FX_H / 2) / cam.focal, 1};
            const float along = dot3(ray, n);
            if (along >= 0) {
                out[x] = tr->palette[fb[x]];
                continue;
            }
            const float k = dot3(o, n) / along;
            const float q[3] = {ray[0] * k - o[0], ray[1] * k - o[1], ray[2] * k - o[2]};
            /* the wall spans the picture, a tile its own part of it */
            const int w = t == &wall_now ? FX_W : TILE_W, h = t == &wall_now ? FX_H : TILE_H;
            const int sx = t->tile % TILE_COLS * TILE_W + (int)floorf(dot3(q, ax) / dot3(ax, ax) * w);
            const int sy = t->tile / TILE_COLS * TILE_H + (int)floorf(dot3(q, ay) / dot3(ay, ay) * h);
            out[x] = dimmed(tr->old_palette[pixel_at(tr->old, sx, sy)], t->shade + 1);
        }
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
    case FX_TR_COLLAPSE:
        collapse_row(tr, y, out);
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
