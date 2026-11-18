/*
 * Every colour of the dongle's screens (src/screen_fx/fx.c draws them), in
 * one place to change them in. Red, green, blue, each 0..255; the panel
 * shows 5, 6 and 5 bits of them.
 *
 * - the colour themes: Keys, Debug, Tracker and Terminal
 * - Thermal's and the three screensavers' own colours, which no theme
 *   touches, and those of the status text over them
 * - the four colours every animation has
 *
 * tools/fx_preview.py draws all of it on a computer (--themes for the
 * themes), which is quicker than flashing to see a change.
 */

#pragma once

#include <stdint.h>

struct color {
    uint8_t r, g, b;
};

/* --- the colour themes ------------------------------------------------------ */

/*
 * The Keys, Debug, Tracker and Terminal screens are drawn in the colours of
 * the theme that is set (the Config layer's Theme key steps through them).
 *
 * To add one, add a line: its name (15 letters at most, in the animations'
 * capitals: it is shown when the theme is turned to, and it is what the
 * dongle saves) and eleven colours, red, green, blue.
 *
 *   bg      the background: black on a dark theme (anything lighter looks
 *           washed out on the backlit panel), or the light one's own
 *   dim     rules, keys that aren't pressed, the unlit part of a meter
 *   label   captions, and readings that aren't known
 *   text    the readings
 *   bright  a key held down, headings, the newest of anything
 *
 * Tracker runs from bg through dim and text up to nearly bright; Terminal
 * is bg, and what is typed flashes bright, then is text, and fades out
 * through dim. On a light theme bright is the darkest of them.
 *
 *   glow    a key just let go (it fades from this to dim), the level meters
 *   good, warn, bad   connected and strong; getting weak or low; lost or flat
 *   mod     a key holding a modifier down
 *   layer   a key holding a layer on
 */
static const struct scheme {
    const char *name;
    struct color bg, dim, label, text, bright, glow, good, warn, bad, mod, layer;
} schemes[] = {
    /* the first is what a dongle has until another is chosen */
    {"ICE", {0, 5, 14}, {0, 45, 70}, {50, 120, 150}, {150, 220, 245}, {255, 255, 255}, {90, 225, 235},
     {70, 235, 130}, {255, 190, 40}, {255, 60, 40}, {255, 150, 30}, {205, 110, 255}},
    /* the green phosphor terminal, as the Terminal screen was before themes */
    {"PHOSPHOR", {0, 0, 0}, {0, 95, 32}, {0, 190, 70}, {40, 255, 90}, {215, 255, 225}, {140, 255, 170},
     {170, 255, 60}, {255, 230, 0}, {255, 50, 50}, {255, 170, 0}, {0, 220, 255}},
    /* the amber phosphor terminal */
    {"AMBER", {0, 0, 0}, {115, 62, 0}, {225, 130, 0}, {255, 176, 0}, {255, 240, 200}, {255, 215, 90},
     {150, 255, 80}, {255, 255, 90}, {255, 40, 30}, {0, 200, 255}, {255, 90, 170}},
    /* Star Wars, the dark side: a red lightsaber on black (bad is magenta, red being everything) */
    {"SITH", {0, 0, 0}, {125, 0, 12}, {235, 30, 40}, {255, 75, 60}, {255, 230, 225}, {255, 140, 110},
     {80, 255, 120}, {255, 215, 0}, {255, 0, 170}, {255, 150, 0}, {90, 170, 255}},
    /* Tron: Legacy, the Grid's cyan on black, the other side in orange */
    {"GRID", {0, 0, 0}, {0, 85, 110}, {0, 175, 210}, {0, 240, 255}, {230, 255, 255}, {130, 250, 255},
     {100, 255, 160}, {255, 230, 60}, {255, 50, 40}, {255, 120, 0}, {255, 240, 140}},
    /* Synthwave '84: neon pink, captioned in cyan, over purple */
    {"SYNTHWAVE", {0, 0, 0}, {95, 30, 125}, {54, 220, 230}, {255, 90, 220}, {255, 255, 255}, {255, 170, 240},
     {114, 241, 184}, {254, 222, 93}, {254, 68, 80}, {249, 150, 100}, {180, 130, 255}},
    /* the Dracula theme's purple and pink, on black */
    {"DRACULA", {0, 0, 0}, {72, 42, 135}, {165, 115, 255}, {205, 165, 255}, {248, 248, 242}, {255, 121, 198},
     {80, 250, 123}, {241, 250, 140}, {255, 85, 85}, {255, 184, 108}, {139, 233, 253}},
    /* Cyberpunk 2077: the yellow, with cyan and red */
    {"CYBERPUNK", {0, 0, 0}, {110, 102, 0}, {205, 195, 0}, {252, 238, 10}, {255, 255, 225}, {2, 215, 242},
     {0, 255, 159}, {255, 140, 0}, {255, 0, 60}, {255, 90, 200}, {170, 120, 255}},
    /* Star Trek's LCARS: orange, captioned in lilac, on black */
    {"LCARS", {0, 0, 0}, {115, 62, 0}, {204, 153, 255}, {255, 153, 0}, {255, 236, 210}, {255, 204, 102},
     {120, 255, 140}, {255, 255, 100}, {255, 60, 60}, {153, 153, 255}, {255, 102, 153}},
    /* the Vim theme, after TextMate's Monokai: white on black, pink captions */
    {"MOLOKAI", {0, 0, 0}, {110, 20, 55}, {249, 38, 114}, {248, 248, 242}, {255, 255, 255}, {102, 217, 239},
     {166, 226, 46}, {230, 219, 116}, {255, 60, 40}, {253, 151, 31}, {174, 129, 255}},
    /* the white phosphor terminal */
    {"MONO", {0, 0, 0}, {85, 85, 85}, {170, 170, 170}, {235, 235, 235}, {255, 255, 255}, {255, 255, 255},
     {70, 255, 120}, {255, 210, 0}, {255, 50, 40}, {255, 150, 30}, {120, 190, 255}},
};
#define SCHEMES ((int)(sizeof(schemes) / sizeof(schemes[0])))


/* --- the animations with colours of their own ------------------------------ */

/*
 * Each is a run of colours along the palette: `at` is the palette entry
 * (0..251, ascending) that is exactly this colour, and the entries between
 * two of them shade from one to the other. What the entries mean is the
 * animation's business, so change the colours and leave the `at`s.
 */
struct stop {
    uint8_t at, r, g, b;
};

/* Thermal: cold to hot, as a heat camera shows it; the flames run on from red to white */
static const struct stop thermal_stops[] = {
    {0, 0, 0, 35},      {45, 0, 25, 150},   {95, 0, 160, 130}, {140, 150, 225, 0},
    {175, 255, 210, 0}, {215, 255, 35, 0},  {251, 255, 245, 235}
};

/* Warp: four colours of 63 brightness levels each, from black: the stars, then three for the comets */
static const struct stop warp_stops[] = {
    {0, 0, 0, 0},   {22, 30, 50, 140},   {50, 170, 200, 255},  {62, 255, 255, 255}, /* stars */
    {63, 0, 0, 0},  {85, 0, 80, 110},    {113, 40, 240, 230},  {125, 220, 255, 255},
    {126, 0, 0, 0}, {148, 130, 45, 0},   {176, 255, 180, 30},  {188, 255, 250, 210},
    {189, 0, 0, 0}, {211, 110, 0, 120},  {239, 255, 70, 220},  {251, 255, 225, 255},
};

/* Code: the same, for the kinds of word in the program */
static const struct stop code_stops[] = {
    {0, 0, 0, 0},   {20, 0, 70, 25},   {50, 40, 255, 100},  {62, 220, 255, 225}, /* green */
    {63, 0, 0, 0},  {83, 0, 60, 80},   {113, 60, 230, 255}, {125, 225, 255, 255},
    {126, 0, 0, 0}, {146, 90, 50, 0},  {176, 255, 190, 40}, {188, 255, 250, 215},
    {189, 0, 0, 0}, {209, 40, 70, 60}, {239, 110, 170, 145}, {251, 200, 235, 220}, /* comments */
};

#define LIFE_TRAIL 50
#define LIFE_OLD 60

/* Life: the glow a dead cell leaves (up to LIFE_TRAIL), then a live one by its age, old (LIFE_OLD) to newborn */
static const struct stop life_stops[] = {
    {0, 0, 0, 0},         {LIFE_TRAIL, 70, 0, 110}, {LIFE_OLD, 0, 60, 170},
    {130, 0, 190, 110},   {200, 210, 235, 0},       {251, 255, 255, 240}
};

/*
 * The battery levels and the output, drawn over each of those: `ink` the
 * levels and the Bluetooth profile's number, `dim` that number when the
 * computer isn't connected, `accent` the wireless symbol when it is and
 * `search` when it isn't (it pulses), `low` a battery that is nearly flat.
 * Over Tracker and Terminal they are the theme's text, label and bad.
 */
struct text_colors {
    struct color ink, dim, accent, search, low;
};

static const struct text_colors thermal_text = {
    {255, 255, 255}, {170, 170, 170}, {255, 255, 255}, {190, 190, 190}, {255, 60, 60}};
static const struct text_colors warp_text = {
    {255, 255, 255}, {160, 160, 160}, {40, 240, 230}, {208, 208, 208}, {255, 70, 50}};
static const struct text_colors code_text = {
    {220, 255, 225}, {110, 170, 145}, {60, 230, 255}, {150, 180, 165}, {255, 70, 50}};
static const struct text_colors life_text = {
    {255, 255, 255}, {160, 160, 160}, {60, 235, 130}, {208, 208, 208}, {255, 70, 50}};

/* --- on every animation ------------------------------------------------------ */

/* A flat battery and a lost link (where the theme has no `bad` of its own),
 * and the box a theme's name shows in. */
static const struct color fixed_black = {0, 0, 0};
static const struct color fixed_red = {255, 60, 40};
static const struct color fixed_grey = {150, 150, 150};
static const struct color fixed_white = {255, 255, 255};
