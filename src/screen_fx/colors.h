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
    /* the Vim theme, after TextMate's Monokai */
    {"MOLOKAI", {0, 0, 0}, {62, 61, 50}, {126, 142, 145}, {248, 248, 242}, {255, 255, 255}, {102, 217, 239},
     {166, 226, 46}, {230, 219, 116}, {249, 38, 114}, {253, 151, 31}, {174, 129, 255}},
    /* the Vim theme */
    {"BADWOLF", {0, 0, 0}, {69, 65, 59}, {153, 143, 132}, {248, 246, 242}, {255, 255, 255}, {10, 157, 255},
     {174, 238, 0}, {250, 222, 62}, {255, 44, 75}, {255, 167, 36}, {255, 158, 184}},
    /* the Vim theme (dark) */
    {"GRUVBOX", {0, 0, 0}, {80, 73, 69}, {168, 153, 132}, {235, 219, 178}, {251, 241, 199}, {142, 192, 124},
     {184, 187, 38}, {250, 189, 47}, {251, 73, 52}, {254, 128, 25}, {211, 134, 155}},
    /* the Neovim theme */
    {"TOKYO NIGHT", {0, 0, 0}, {59, 66, 97}, {115, 122, 162}, {192, 202, 245}, {233, 236, 255}, {125, 207, 255},
     {158, 206, 106}, {224, 175, 104}, {247, 118, 142}, {255, 158, 100}, {187, 154, 247}},
    /* the theme (Mocha) */
    {"CATPPUCCIN", {0, 0, 0}, {69, 71, 90}, {147, 153, 178}, {205, 214, 244}, {245, 224, 220}, {137, 220, 235},
     {166, 227, 161}, {249, 226, 175}, {243, 139, 168}, {250, 179, 135}, {203, 166, 247}},
    /* the theme (Rosé Pine) */
    {"ROSE PINE", {0, 0, 0}, {64, 61, 82}, {144, 140, 170}, {224, 222, 244}, {250, 244, 237}, {196, 167, 231},
     {156, 207, 216}, {246, 193, 119}, {235, 111, 146}, {235, 188, 186}, {62, 143, 176}},
    /* the Neovim theme (Wave) */
    {"KANAGAWA", {0, 0, 0}, {54, 54, 70}, {147, 138, 169}, {220, 215, 186}, {242, 236, 188}, {127, 180, 202},
     {152, 187, 108}, {230, 195, 132}, {255, 93, 98}, {255, 160, 102}, {149, 127, 184}},
    /* GitHub's light theme */
    {"GITHUB LIGHT", {255, 255, 255}, {208, 215, 222}, {110, 119, 129}, {36, 41, 47}, {0, 0, 0}, {9, 105, 218},
     {26, 127, 55}, {154, 103, 0}, {207, 34, 46}, {188, 76, 0}, {130, 80, 223}},
    /* Catppuccin's light theme */
    {"LATTE", {239, 241, 245}, {188, 192, 204}, {108, 111, 133}, {76, 79, 105}, {30, 30, 46}, {30, 102, 245},
     {64, 160, 43}, {223, 142, 29}, {210, 15, 57}, {254, 100, 11}, {136, 57, 239}},
    /* the Vim theme (light) */
    {"GRUVBOX LIGHT", {251, 241, 199}, {213, 196, 161}, {124, 111, 100}, {60, 56, 54}, {40, 40, 40}, {7, 102, 120},
     {121, 116, 14}, {181, 118, 20}, {157, 0, 6}, {175, 58, 3}, {143, 63, 113}},
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
