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
 *   bg      the background: keep it nearly black, the panel is backlit
 *   dim     rules, keys that aren't pressed, the unlit part of a meter
 *   label   captions, and readings that aren't known
 *   text    the readings
 *   bright  a key held down, headings, the newest of anything
 *
 * Tracker runs from bg through dim and text up to nearly white; Terminal
 * is black, and what is typed flashes bright, then is text, and fades out
 * through dim.
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
    /* Tron: the Grid's cyan on black, captioned in the other side's orange */
    {"GRID", {0, 2, 4}, {0, 50, 64}, {190, 92, 0}, {70, 230, 255}, {240, 255, 255}, {130, 245, 255},
     {120, 255, 190}, {255, 215, 70}, {255, 50, 60}, {255, 120, 0}, {255, 240, 150}},
    /* Alien: the Nostromo's computer, which is also the green phosphor terminal */
    {"MOTHER", {0, 7, 2}, {0, 58, 22}, {25, 135, 60}, {70, 250, 110}, {225, 255, 230}, {150, 255, 170},
     {170, 255, 190}, {255, 190, 50}, {255, 70, 50}, {255, 170, 40}, {110, 215, 255}},
    /* Alien, the animal: acid green over a violet dark */
    {"XENO", {5, 3, 12}, {42, 34, 78}, {105, 115, 95}, {190, 240, 90}, {245, 255, 225}, {215, 255, 130},
     {80, 235, 170}, {255, 200, 60}, {255, 70, 90}, {255, 140, 70}, {185, 125, 255}},
    /* Star Wars, the dark side: black and crimson, with one lightsaber of each colour */
    {"SITH", {7, 0, 1}, {72, 10, 14}, {165, 45, 42}, {240, 200, 192}, {255, 255, 255}, {255, 95, 70},
     {120, 225, 140}, {255, 200, 70}, {255, 40, 40}, {255, 165, 60}, {110, 170, 255}},
    /* the amber terminal */
    {"AMBER", {8, 4, 0}, {76, 40, 0}, {168, 98, 0}, {255, 176, 0}, {255, 238, 195}, {255, 212, 105},
     {150, 232, 115}, {255, 242, 100}, {255, 65, 40}, {90, 215, 255}, {255, 120, 160}},
    /* Star Trek's LCARS: peach, lilac and periwinkle on black */
    {"LCARS", {0, 0, 0}, {66, 48, 76}, {170, 125, 185}, {255, 204, 153}, {255, 246, 232}, {255, 153, 0},
     {150, 225, 165}, {255, 225, 100}, {235, 70, 70}, {153, 153, 255}, {220, 105, 165}},
    /* Synthwave '84: pink and cyan neon */
    {"NEON", {10, 5, 20}, {58, 42, 96}, {132, 139, 189}, {255, 126, 219}, {255, 255, 255}, {255, 190, 240},
     {114, 241, 184}, {254, 222, 93}, {254, 68, 80}, {249, 150, 100}, {54, 249, 246}},
    /* the terminal themes of those names, on darker backgrounds than their own */
    {"DRACULA", {20, 21, 30}, {68, 71, 90}, {98, 114, 164}, {235, 235, 228}, {255, 255, 255}, {139, 233, 253},
     {80, 250, 123}, {241, 250, 140}, {255, 85, 85}, {255, 184, 108}, {255, 121, 198}},
    {"GRUVBOX", {20, 22, 23}, {80, 73, 69}, {146, 131, 116}, {235, 219, 178}, {251, 241, 199}, {142, 192, 124},
     {184, 187, 38}, {250, 189, 47}, {251, 73, 52}, {254, 128, 25}, {211, 134, 155}},
    {"SOLARIZED", {0, 24, 31}, {12, 68, 80}, {88, 110, 117}, {160, 175, 172}, {253, 246, 227}, {42, 161, 152},
     {133, 153, 0}, {212, 160, 20}, {220, 50, 47}, {225, 95, 40}, {125, 130, 225}},
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
