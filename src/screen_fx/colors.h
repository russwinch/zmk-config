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
 *
 * tools/fx_preview.py --contrast checks each colour can be read on bg
 * (WCAG contrast ratios); the light ones are their themes' colours made
 * darker or lighter, never another hue, until they pass at 6 or more.
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
    /* TextMate's Monokai: white on black, pink captions */
    {"MONOKAI", {0, 0, 0}, {110, 20, 55}, {249, 38, 114}, {248, 248, 242}, {255, 255, 255}, {102, 217, 239},
     {166, 226, 46}, {230, 219, 116}, {255, 60, 40}, {253, 151, 31}, {174, 129, 255}},
    /* the editor theme: its yellow and orange, on black */
    {"GRUVBOX", {0, 0, 0}, {110, 55, 10}, {254, 128, 25}, {250, 189, 47}, {251, 241, 199}, {142, 192, 124},
     {184, 187, 38}, {255, 230, 90}, {251, 73, 52}, {131, 165, 152}, {211, 134, 155}},
    /* the editor theme: its blue and magenta, on black */
    {"TOKYO NIGHT", {0, 0, 0}, {60, 50, 120}, {187, 154, 247}, {122, 162, 247}, {230, 235, 255}, {125, 207, 255},
     {158, 206, 106}, {224, 175, 104}, {247, 118, 142}, {255, 158, 100}, {115, 218, 202}},
    /* the editor theme (Mocha): its mauve and blue, on black */
    {"CATPPUCCIN", {0, 0, 0}, {70, 50, 110}, {137, 180, 250}, {203, 166, 247}, {205, 214, 244}, {245, 194, 231},
     {166, 227, 161}, {249, 226, 175}, {243, 139, 168}, {250, 179, 135}, {148, 226, 213}},
    /* the editor theme: its frost blues, on black */
    {"NORD", {0, 0, 0}, {40, 60, 85}, {129, 161, 193}, {136, 192, 208}, {236, 239, 244}, {143, 188, 187},
     {163, 190, 140}, {235, 203, 139}, {191, 97, 106}, {208, 135, 112}, {180, 142, 173}},
    /* the white phosphor terminal */
    {"MONO", {0, 0, 0}, {85, 85, 85}, {170, 170, 170}, {235, 235, 235}, {255, 255, 255}, {255, 255, 255},
     {70, 255, 120}, {255, 210, 0}, {255, 50, 40}, {255, 150, 30}, {120, 190, 255}},
    /* GitHub's light theme: near-black on white, captioned in its blue */
    {"GITHUB LIGHT", {255, 255, 255}, {165, 209, 237}, {6, 95, 200}, {31, 35, 40}, {0, 0, 0}, {6, 95, 200},
     {0, 115, 43}, {134, 90, 8}, {196, 15, 37}, {167, 67, 2}, {117, 65, 208}},
    /* Catppuccin's light theme: slate on pale grey, captioned in mauve */
    {"LATTE", {239, 241, 245}, {198, 185, 230}, {121, 32, 220}, {54, 56, 81}, {17, 17, 27}, {0, 77, 219},
     {25, 105, 1}, {129, 79, 1}, {183, 5, 47}, {156, 59, 5}, {7, 101, 106}},
    /* Gruvbox's light theme (hard): dark grey on cream, captioned in orange */
    {"GRUVBOX LIGHT", {249, 245, 215}, {223, 189, 143}, {166, 54, 0}, {60, 56, 54}, {29, 32, 33}, {4, 101, 119},
     {98, 94, 3}, {130, 82, 2}, {157, 0, 6}, {45, 103, 69}, {143, 63, 113}},
    /* Atom's One Light: dark grey on white, captioned in its cyan */
    {"ONE LIGHT", {250, 250, 250}, {199, 199, 203}, {8, 101, 144}, {56, 58, 66}, {16, 17, 20}, {34, 87, 206},
     {24, 111, 28}, {130, 88, 5}, {182, 40, 33}, {130, 88, 3}, {164, 36, 162}},
    /* the PaperColor theme: grey ink on paper, blue captions, pink glow */
    {"PAPERCOLOR", {238, 238, 238}, {190, 190, 190}, {0, 95, 135}, {56, 56, 56}, {0, 0, 0}, {172, 10, 108},
     {77, 96, 2}, {149, 63, 0}, {175, 0, 0}, {135, 0, 175}, {1, 96, 126}},
    /* Solarized Light: its cream, ink and blue, darkened to read */
    {"SOLARIZED", {253, 246, 227}, {204, 196, 170}, {2, 98, 156}, {44, 64, 71}, {0, 43, 54}, {7, 105, 98},
     {86, 100, 7}, {118, 89, 6}, {191, 0, 18}, {166, 57, 7}, {82, 85, 165}},
    /* the original Game Boy's green screen, its four greens */
    {"GAME BOY", {155, 188, 15}, {119, 147, 15}, {1, 56, 6}, {0, 0, 0}, {0, 0, 0}, {1, 56, 6},
     {0, 57, 4}, {64, 44, 2}, {103, 3, 2}, {45, 30, 121}, {0, 50, 81}},
    /* Norton Commander: the PC's text-mode blue, cyan and yellow */
    {"NORTON", {0, 0, 170}, {0, 49, 233}, {85, 255, 255}, {255, 255, 255}, {255, 255, 85}, {255, 255, 85},
     {85, 255, 85}, {255, 204, 85}, {254, 143, 136}, {255, 123, 253}, {255, 170, 85}},
    /* the Amiga's Workbench: blue, white and orange */
    {"AMIGA", {0, 85, 170}, {0, 42, 102}, {254, 190, 144}, {255, 255, 255}, {255, 255, 255}, {214, 237, 255},
     {162, 255, 176}, {255, 235, 143}, {255, 228, 226}, {255, 230, 206}, {254, 225, 254}},
    /* Wes Bos's Cobalt2: white and yellow on deep blue */
    {"COBALT2", {25, 53, 73}, {46, 85, 114}, {255, 157, 0}, {225, 239, 255}, {255, 255, 255}, {158, 255, 255},
     {58, 217, 0}, {255, 198, 0}, {254, 148, 170}, {251, 148, 255}, {124, 183, 254}},
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
static const struct text_colors showroom_text = {
    {255, 255, 255}, {160, 160, 160}, {255, 150, 60}, {208, 208, 208}, {255, 70, 50}};

/*
 * Showroom: the board in 3D, in the colours of the README's pictures
 * (tools/boards.py). The backdrop runs from its edges to where the
 * spotlight falls, and darker where something's shadow is on it. Each
 * material is in five bands: turned from the light, half lit, lit, caught
 * by the light from behind, and a glint sweeping over it. A key just
 * pressed glows, from its own colour to `showroom_glow`.
 */
static const struct color showroom_backdrop[2] = {{3, 4, 9}, {44, 50, 70}};
static const struct color showroom_shadow[2] = {{1, 2, 4}, {16, 18, 28}};
static const struct color showroom_materials[][5] = {
    {{30, 33, 40}, {50, 55, 63}, {72, 79, 90}, {120, 150, 190}, {190, 202, 220}},      /* the case: #3d434c */
    {{22, 25, 30}, {40, 44, 51}, {58, 64, 73}, {100, 128, 165}, {170, 182, 200}},      /* the cover: #30353c */
    {{100, 106, 120}, {160, 166, 178}, {205, 211, 220}, {190, 214, 245}, {255, 255, 255}}, /* keycaps: #c4cad3 */
    {{118, 76, 22}, {190, 130, 44}, {232, 170, 70}, {255, 214, 150}, {255, 245, 222}}, /* the extra keys, dials, rollers: #e3a33c */
    {{78, 94, 86}, {128, 148, 136}, {164, 186, 171}, {190, 214, 200}, {232, 242, 236}}, /* the halves' screens: #9fb4a6 */
    {{14, 15, 18}, {28, 30, 35}, {44, 47, 54}, {80, 100, 130}, {140, 150, 170}},      /* switches, rollers' ribs */
    {{12, 14, 17}, {24, 27, 31}, {36, 40, 46}, {70, 90, 120}, {130, 150, 180}},       /* screen edges: #1d2126 */
    {{20, 19, 23}, {40, 38, 44}, {64, 61, 70}, {110, 140, 190}, {180, 188, 205}},      /* the dongle, and the tiles' backs */
};
static const struct color showroom_glow = {110, 230, 255};
static const struct color showroom_ink = {2, 3, 6};
static const struct color showroom_caption = {235, 238, 245};

/*
 * Clash: Showroom's board in neon. Each material's five bands (as
 * Showroom's) are dark, so the solids read by their lit edges; a creased
 * edge within a solid is dimmer. The floor's grid runs from black to
 * `clash_grid`, the horizon glows `clash_horizon`. A pressed key fills
 * through `clash_hot`, glowing round it. A shortcut's impact frame is in
 * `clash_red` with `clash_dark` and `clash_light`.
 */
static const struct text_colors clash_text = {
    {255, 255, 255}, {160, 160, 160}, {0, 235, 255}, {208, 208, 208}, {255, 70, 50}};
static const struct color clash_materials[][5] = {
    {{4, 6, 14}, {8, 11, 24}, {13, 18, 36}, {20, 40, 64}, {13, 18, 36}},  /* the case */
    {{4, 6, 14}, {8, 11, 24}, {13, 18, 36}, {20, 40, 64}, {13, 18, 36}},  /* the cover */
    {{8, 11, 26}, {14, 20, 42}, {22, 30, 60}, {30, 60, 90}, {22, 30, 60}}, /* keycaps */
    {{22, 12, 4}, {38, 22, 8}, {58, 34, 12}, {80, 50, 20}, {58, 34, 12}},   /* the extra keys, dials, rollers */
    {{6, 16, 18}, {10, 26, 30}, {16, 38, 44}, {24, 56, 64}, {16, 38, 44}},  /* the halves' screens */
    {{3, 4, 9}, {6, 8, 16}, {9, 12, 22}, {14, 22, 36}, {9, 12, 22}},        /* switches, rollers' ribs */
    {{3, 4, 9}, {6, 8, 16}, {9, 12, 22}, {14, 22, 36}, {9, 12, 22}},        /* screen edges */
    {{5, 6, 14}, {9, 11, 24}, {14, 17, 34}, {22, 36, 60}, {14, 17, 34}},    /* the dongle */
};
static const struct color clash_edges[] = {
    {0, 130, 160}, {0, 130, 160}, {0, 235, 255}, {255, 170, 40}, {80, 220, 200}, {0, 90, 120}, {0, 110, 140}, {0, 150, 190}};
static const struct color clash_hot[3][2] = {
    /* fill, edge: just warm, warm, just pressed */
    {{70, 10, 60}, {170, 40, 150}}, {{150, 20, 120}, {240, 60, 200}}, {{255, 40, 190}, {255, 190, 240}}};
static const struct color clash_grid = {0, 170, 210};
static const struct color clash_horizon = {150, 20, 120};
static const struct color clash_red = {225, 30, 50};
static const struct color clash_dark = {14, 4, 10};
static const struct color clash_light = {255, 244, 236};

/* --- on every animation ------------------------------------------------------ */

/* A flat battery and a lost link (where the theme has no `bad` of its own),
 * and the box a theme's name shows in. */
static const struct color fixed_black = {0, 0, 0};
static const struct color fixed_red = {255, 60, 40};
static const struct color fixed_grey = {150, 150, 150};
static const struct color fixed_white = {255, 255, 255};
