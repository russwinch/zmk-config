# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**
(`config/keymap.dtsi`). Every board gets the same 34 core keys and layers;
each board's own file only adds its extras.

| Keyboard | Controller | Display / LEDs | Extras |
| --- | --- | --- | --- |
| Urchin | nice!nano v2 | RGB LED widget (no screens) | optional Prospector dongle |
| splitkb Aurora Corne | nice!nano v2 | nice!view on the left half, RGB LED widget | 2 encoders, outer thumb keys, an outer column per side; optional Prospector dongle |
| Waterfowl | nice!nano v2 | nice!view on the left half, RGB LED widget | 4 encoders, 2 extra thumb keys per side; optional Prospector dongle |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED widget | optional Prospector dongle |

## Keymap

![All layers](keymap-drawer/keymap.svg)

The picture is generated: the "Draw keymap" workflow redraws it and commits it
whenever the keymap changes. To preview locally, run `keymap-drawer/draw.sh`
(needs `pip install keymap-drawer`). Modifiers are drawn with Mac symbols; on
the Windows layer read ⌘ as Win.

| Layer | How to get there | What's on it |
| --- | --- | --- |
| Mac | default | QWERTY, home-row mods Ctrl Alt Cmd Shift / Shift Cmd Alt Ctrl |
| Windows | Config layer → Windows | Mac layer with Ctrl and Win swapped |
| Lower | hold Esc | lock screen (Q), media, browser tabs, desktops, app windows (E), screenshots (X: drag out an area, to the clipboard; Z: the whole screen, to a file on the desktop), Mission Control (C), the app switcher (B: Cmd+Tab, V: Cmd+Shift+Tab; Cmd stays held until Esc/Lower is let go, so tap on through the apps), £ (comma), arrows, symbols |
| Raise | hold Enter | numbers, shifted numbers, brackets |
| Config | hold Esc + Enter | Mac/Windows switch, battery level, Bluetooth, dongle screen prev/next (Y and U), colour theme prev/next (I and O) and brightness (P) (shown as "Shit" on the screens) |

The Lower layer's lock key sends Ctrl+Cmd+Q, the Mac's Lock Screen shortcut:
the Mac locks at once and its display stays on until it times out. It is
Mac only (Windows locks with Win+L). On a dongle it also starts
the [screensaver](#animated-screens) when you let go of it. The battery key blinks each
half's battery level on its RGB LED (green, yellow, red). The LED also flashes
the Bluetooth connection state when it changes. Both flashes last 150ms
(set for every board in `config/default.conf`).

### Timing

- **Home-row mods** use urob's "timeless" setup: a mod only fires when the next
  key is on the other hand or a thumb. `require-prior-idle-ms` is 100 (urob
  uses 150), so shortcuts typed right after a word still work.
- **Space/Shift thumb** (`&ts`) is balanced: Shift as soon as a key is pressed
  and released while Space is held, so fast capitals work.
- **Backspace/Cmd thumb** is tap-preferred with a 250ms term, so rolling
  Backspace into a letter never sends Cmd+letter.
- **Esc/Lower and Enter/Raise** are hold-preferred: any key pressed while the
  thumb is down uses the layer.

### Sleep

| Board | Deep sleep after | Display |
| --- | --- | --- |
| Urchin | 1 hour | none |
| Aurora Corne | 1 hour | left nice!view stays on (it uses next to no power) |
| Forager | 1 hour | none |
| Waterfowl halves | 1 hour | left nice!view stays on |
| Prospector dongles | never (USB powered) | panel and backlight off after an hour idle (backlight PWM kept running at its shortest pulse); any key brings it back |

Deep sleep drops the Bluetooth connection: the next key press wakes the board
(that key isn't typed) and it takes a second or so to reconnect. Nothing sleeps
while it's on USB power. The sleep timeout is shared by every board in
`config/default.conf`; the dongle's exception is in `build.yaml`.

### Board extras

| Board | Encoders (turn) | Extra keys |
| --- | --- | --- |
| Urchin, Forager | none | none |
| Aurora Corne | left: volume; right: up/down (page up/down on Lower) | outer thumbs: mute (left), play/pause (right); outer columns, top to bottom: Tab, Esc (hold: Ctrl), Shift on the left, Del, ', Enter (hold: Shift) on the right, the same on every layer |
| Waterfowl | left roller (outer): scrolls up/down, a mouse wheel notch a click; left dial (inner): volume; right dial (inner): up/down (page up/down on Lower); right roller (outer): scrolls left/right | pushes: left roller Home (top of the page), right roller End (bottom), left dial mute, right dial play/pause; the outer thumb keys repeat the thumb next to them. The scrolling is a mouse: over Bluetooth the computer has to forget the keyboard and pair again before it scrolls |

## Layout of this repo

| File | Purpose |
| --- | --- |
| `config/keymap.dtsi` | **The keymap.** All layers and behaviors, written once as 34 core keys. |
| `config/<shield>.keymap` | Per-board glue: maps the 34 core keys onto that board (extra keys, encoders), then includes `keymap.dtsi`. |
| `config/default.conf` | Settings shared by every build (deep sleep, Bluetooth power, LED flash times). ZMK applies it after the other `.conf` files, so exceptions go in `build.yaml` as `cmake-args`. |
| `config/prospector_adapter.conf` | Settings shared by every Prospector dongle (status screen, brightness, animated screens). ZMK reads `config/<shield>.conf` for each shield in a build, and every dongle build has the `prospector_adapter` shield. |
| `config/nice_view.conf` | The same way, for every left half with a nice!view (Waterfowl, Aurora Corne): turns the display on. |
| `config/<keyboard>.conf` | Only what is particular to one keyboard (encoders, LED widget, display options); also read for its dongle. A `config/<keyboard>_dongle.conf` would be read for that dongle alone, but none is needed. |
| `config/<keyboard>_layout.dtsi` | Glue shared by a keyboard's normal and dongle keymaps (Urchin, Aurora Corne, Waterfowl; the Forager dongle uses `forager.keymap` itself). |
| `config/half_status.dtsi` | The behavior the dongle sends its status to, for the left half's nice!view (Waterfowl and Aurora Corne keymaps). |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `boards/shields/<keyboard>_dongle/` | Keyless dongle shields for the Waterfowl, Urchin and Aurora Corne (used with `prospector_adapter`); the Forager's comes from its module. |
| `src/`, `CMakeLists.txt`, `Kconfig`, `dts/` | This repo's own code (it is a Zephyr module, `zephyr/module.yml`): dims the dongle's screen when idle, the dongle's animated screens, the left half's nice!view screen when behind the dongle, and the Mac app switcher keys. |
| `config/west.yml` | ZMK version + modules. ZMK is pinned to a `main` commit (Zephyr 4.1), modules to commits that match it. |
| `build.yaml` | Which firmware CI builds. |
| `tools/fx_preview.py` | Renders the dongle's animated screens to GIFs on a computer (see With the Prospector dongle). |
| `tools/half_status_preview.py` | Renders the left half's nice!view screen behind the dongle on a computer. |
| `tools/flash.py` | Flashes a keyboard's devices in order, optionally with a settings reset (see Flashing). |
| `keymap-drawer/` | Settings and script that draw `keymap.svg` (run by `.github/workflows/draw.yml`). |

## Flashing

Every push builds all firmware (`.github/workflows/build.yml`) and publishes
it as a release named after the branch and commit (the newest 30 are kept;
builds of branches other than `main` are prereleases). Download
`firmware.zip` from the release and flash each half (double-tap reset, then
copy the `.uf2` onto the drive that appears). Each `.uf2` is attached to the
release on its own as well, with dots for the spaces in its name.

On an iPad, download the `.uf2` in Safari and use Share > Save to Files to
put it on the bootloader drive: copy and paste in the Files app fails there
(error 100093).

[Keyboard Writer](https://github.com/russwinch/keyboard-writer) is a macOS
app that does all of this: plug the devices in, press Start, and each is
flashed in order without touching reset. It lists the releases here and
downloads the one chosen. The firmware here includes its
module (`config/west.yml`, `config/default.overlay`) so the app can identify
each device and start its bootloader over USB.

`tools/flash.py` does the copying from a terminal: it waits for each bootloader drive
and copies the right file, in order, so you only double-tap reset. It needs
Python 3.9+ and nothing else.

```sh
python3 tools/flash.py waterfowl-dongle            # newest ~/Downloads/firmware*.zip
python3 tools/flash.py waterfowl-dongle --reset    # settings_reset first on each device
python3 tools/flash.py waterfowl --firmware path/to/firmware.zip
python3 tools/flash.py waterfowl-dongle --download # latest main build, needs the gh CLI
python3 tools/flash.py waterfowl-dongle --only dongle
python3 tools/flash.py --list                      # keyboards and their devices
```

With `--reset` each device is done twice (settings reset, then firmware), so
double-tap reset again when it asks. `--dry-run` shows the plan without
flashing. It stops rather than copy onto a board of the wrong type.

| Keyboard | Left half | Right half |
| --- | --- | --- |
| Urchin | `urchin_left …` | `urchin_right …` |
| Aurora Corne | `splitkb_aurora_corne_left …` | `splitkb_aurora_corne_right …` |
| Waterfowl | `waterfowl_left …` | `waterfowl_right …` |
| Forager | `forager_left …` | `forager_right …` |

With the Prospector dongle:

| Keyboard | Dongle (`xiao_ble`) | Left half | Right half |
| --- | --- | --- | --- |
| Urchin | `urchin_dongle prospector_adapter …` | `urchin_left_peripheral_for_dongle` | `urchin_right …` |
| Aurora Corne | `splitkb_aurora_corne_dongle prospector_adapter …` | `splitkb_aurora_corne_left_peripheral_for_dongle` | `splitkb_aurora_corne_right …` |
| Waterfowl | `waterfowl_dongle prospector_adapter …` | `waterfowl_left_peripheral_for_dongle` | `waterfowl_right …` |
| Forager | `forager_dongle prospector_adapter …` | `forager_left_peripheral_for_dongle` | `forager_right …` |

If halves won't pair, flash `settings_reset` (one per controller type:
`nice_nano` or `xiao_ble`) to both halves, then the normal
firmware again.

### With the Prospector dongle

Every keyboard can run behind a Prospector dongle (`<keyboard>-dongle` in
`tools/flash.py --list`). The steps below use the Waterfowl's names; the
others follow the same pattern (table above). The dongle becomes the split central (it runs the keymap and plugs into the
computer); both halves become peripherals. To switch:

Quickest: `python3 tools/flash.py waterfowl-dongle --reset` does all of the
steps below in order. By hand:

1. Flash `settings_reset` to the dongle (`xiao_ble`) and both halves (`nice_nano`).
2. Flash `waterfowl_dongle prospector_adapter` to the dongle,
   `waterfowl_left_peripheral_for_dongle` to the left half and the normal
   `waterfowl_right` to the right half.
3. Power on the **left half first, then the right**: the Prospector orders its
   battery widgets by pairing order.

The Config layer's battery key works with the dongle too: the dongle passes it
on and each half blinks its own battery level.

The right halves have no screens (and the Forager's and Urchin's none at
all): they keep their RGB LED. Every left half
with a nice!view (Waterfowl, Aurora Corne), on its own or behind the dongle,
shows a small status screen in place of ZMK's stock picture
(`config/nice_view.conf`), read with the screen vertical:

| Part | Shows |
| --- | --- |
| Top: `L` | this half's battery, as a big number with a small `%` and a bar (100 a size smaller, to fit). On USB power the reading is the charger's voltage (near 100 whatever the charge): behind the dongle, the dongle's estimate instead (see [Debug screen](#debug-screen)), with a bolt flashing slowly (a second on, a second off) in place of the `L`, kept after unplugging until the voltage has settled; on its own, or until the dongle has an estimate (up to a minute after plugging in), just a big bolt, so unplug for a minute or so to see the real level |
| Middle: `R` | the right half's battery (`--` until it has been heard from): on its own the left half fetches it; behind the dongle, the dongle sends it. A big bolt in its place while the right half is on USB power: on its own the left half reads that from the right half as the dongle does (`src/dongle_half_info.c`); behind the dongle, the dongle passes it on (a dongle on older firmware never does), with its estimate of the level, shown as for `L` |
| Bottom | the output (the keyboard's own, or the dongle's): the USB symbol, or Bluetooth and the active profile's number (filled circle: connected; dashed: paired, not connected; plain: not paired). ↻ while waiting for the dongle's first status, ✕ when this half isn't connected to the dongle |

On its own the left half is the central and knows all of this itself. Behind
the dongle, the dongle sends it when the profile, output or a battery changes, and again
every minute (`src/dongle_half_status.c`, `src/half_status_screen.c`). The
half only redraws its screen when something on it has changed.
`tools/half_status_preview.py` draws the screen (`src/half_status_draw.c`) on a
computer for a few made-up states. It uses
ZMK's existing "run a behavior on a half" message: a `hstatus` behavior in
`config/half_status.dtsi` that no key is bound to. The `R` battery is the
dongle's last reading, so it can be out of date while the right half sleeps.

The dongle's screen shows this repo's own screens, below. The Prospector
module's status screen (the **Operator** layout: layer name, battery levels,
modifiers, the output and a words-per-minute meter) is still built but not
shown; `CONFIG_DONGLE_SCREEN_FX_MODULE_SCREEN=y` in
`config/prospector_adapter.conf` puts it back as the first screen, and the
layout line there picks Classic, Radii or Field instead.

#### Animated screens

The **Screen next** key on the Config layer (the U key) steps the dongle
through the [Debug screen](#debug-screen), the [Status screen](#status-screen), the Keys screen and three
full-screen animations, and round again; **Screen prev** (the Y key)
goes the other way. It starts on Keys. The screen you leave it on is
remembered: once it has been showing for 30 seconds it is saved, and the
dongle starts on it after being unplugged. A settings reset forgets it.

The typing speed the screens show and follow is the dongle's own, counted
as a typing test counts it rather than as ZMK does: the characters typed
(letters, figures, symbols, space, Tab and Enter, not Shift, Cmd or
shortcuts), less one for each Backspace, five to a word, over the last 10
seconds (from rest, over as long as the typing has been going, 3 seconds at
least). It is worked out once a second. ZMK's own counts every key code,
Backspace and the modifiers too, over a window that starts again every 5
seconds, so it jumped and read high. The Prospector module's own screens get
this figure too (`src/dongle_wpm.c`).

The **Theme next** key (the O key) steps through the [colour
themes](#colour-themes) of Keys, Status, Debug, Tracker and Terminal; **Theme prev**
(the I key) goes the other way.

| # | Screen | Idle | Typing |
| --- | --- | --- | --- |
| 1 | Debug | a page of readings: [see below](#debug-screen) | |
| 2 | Status | the batteries, the signal and the keys, as a racing car's steering wheel display shows a car's: [see below](#status-screen) | the keys light as on Keys, and the typing speed lights a row of lights along the top |
| 3 | Keys | the three links, a picture of the keys and a level meter for each column: [see below](#keys-screen) | each key lights white while held and glows for two seconds after; orange while it is holding a modifier down, violet while it is holding a layer on (the ICE theme's colours: each [theme](#colour-themes) has its own) |
| 4 | Thermal | cold blue, two warm patches wandering | the board through a heat camera: each key leaves a hot spot at its own place that spreads as it slowly cools, so the keys of a word run together; a sight follows the last key. From 50 wpm the keys catch fire: each burns for a second or two after it is pressed, with a plume of smoke above. A key held down stays hot and gets hotter and wider, and after 5 seconds catches at any speed and burns until it is let go |
| 5 | Tracker | a motion tracker's empty rings and pulse | contacts close in, more and nearer with speed; each key pulls one round to its column; the figure is the nearest one's range |
| 6 | Terminal | a blinking cursor | what you type, in the theme's colour (green in PHOSPHOR), fading out over about 10 seconds |

Three more animations are screensavers, not screens to choose. After 15
minutes without a key (or an encoder turn), or when the Lower layer's
lock key is let go, one of them, picked at random,
takes over from whichever screen is showing, coming in over 18 seconds by
distorting the picture that was there:

| Screensaver | The way in | Left alone | Typing (on the way out) |
| --- | --- | --- | --- |
| Warp | the picture is torn open at the vanishing point and streaks past as the stars reach light speed | drifting stars | light speed down a tunnel; each key fires comets out through its place on the screen, and the view leans towards the side being typed on |
| Code | the picture runs down the screen like wax; nothing is written until it has all gone | a program in an alien script, written slowly down the page from the top, scrolling once the page is full | written fast: two characters a key (the same key always gives the same one) |
| Life | the picture's bright parts come alive in spreading patches, and the cells eat it | Conway's Game of Life, three generations a second | up to fifteen a second; each key drops a clump of cells at its own place; cells age from white through green to blue |

The first key starts the way back, which then runs its 9 seconds
whether or not you keep typing: the screensaver reacts to the typing, starts
to glitch lightly after 2 seconds and harder and harder until, in the last
second, the screen you had cuts back in through it block by block. An
animation that comes back starts afresh. The wait is
`CONFIG_DONGLE_SCREEN_FX_SAVER_MINUTES` (0 for no screensaver, the lock key's
included).

After an hour without a key the panel goes off, as an old television did:
the picture closes up and down on to a white line, the line to a dot, the
dot dies away, and then the backlight fades out. A key brings it back with
the backlight fading up over a second and the screen you had chosen (not the
screensaver) coming through white noise. The dongle starts the same way
when it is plugged in.

The **Screen bright** key (the P key) steps the backlight through low,
medium and high (20, 50 and 90%: `CONFIG_WATERFOWL_DONGLE_BRIGHTNESS_LOW`,
`CONFIG_PROSPECTOR_FIXED_BRIGHTNESS` and `CONFIG_WATERFOWL_DONGLE_BRIGHTNESS_HIGH`
in `config/prospector_adapter.conf`), and round again; which shows on the
screen for a second and a half. A dongle starts on medium, and the level is
remembered as the screen and the theme are, after the same 30 seconds.

#### Colour themes

Keys, Status, Debug, Tracker and Terminal are drawn in the colours of a theme, which the
Config layer's **Theme next** key (the O key) steps through, and round
again (**Theme prev**, the I key, goes back). The screen showing changes colour as it is, and the theme's name
and its place in the list (`PHOSPHOR 2/25`) show in the middle of the screen
for a second and a half, on any screen. Thermal and the screensavers keep
their own colours: there the name is all that shows.
The theme is remembered as the screen is, 30 seconds after it was last
changed (`CONFIG_DONGLE_SCREEN_FX_SAVE_DELAY_MS`, for both).

| # | Theme | After | Looks like |
| --- | --- | --- | --- |
| 1 | ICE | | pale blue on navy, with orange and violet: what a dongle has until another is chosen |
| 2 | PHOSPHOR | the green phosphor terminal | bright green on black, as Terminal was before themes |
| 3 | AMBER | the amber phosphor terminal | amber on black |
| 4 | SITH | Star Wars | red on black; lost or flat is magenta, as red is everywhere |
| 5 | GRID | Tron: Legacy | electric cyan on black, orange for modifiers |
| 6 | SYNTHWAVE | Synthwave '84 | neon pink, cyan captions, purple keys |
| 7 | DRACULA | the editor theme | lilac and pink on black |
| 8 | CYBERPUNK | Cyberpunk 2077 | yellow on black, cyan glow |
| 9 | LCARS | Star Trek | orange, lilac captions, on black |
| 10 | MONOKAI | TextMate's theme | white on black, hot pink captions |
| 11 | GRUVBOX | the editor theme | yellow and orange on black, aqua glow |
| 12 | TOKYO NIGHT | the editor theme | blue and magenta on black |
| 13 | CATPPUCCIN | the editor theme (Mocha) | mauve and blue pastels on black |
| 14 | NORD | the editor theme | frost blues on black |
| 15 | MONO | the white phosphor terminal | white and greys on black |
| 16 | GITHUB LIGHT | GitHub's light theme | near-black on white, blue captions |
| 17 | LATTE | Catppuccin's light theme | slate on pale grey, mauve captions |
| 18 | GRUVBOX LIGHT | Gruvbox's light theme | dark grey on cream, orange captions |
| 19 | ONE LIGHT | Atom's One Light | dark grey on white, cyan captions |
| 20 | PAPERCOLOR | the PaperColor theme | grey ink on paper, blue captions |
| 21 | SOLARIZED | Solarized Light | ink on cream, blue captions |
| 22 | GAME BOY | the original Game Boy's screen | dark greens on pea green |
| 23 | NORTON | Norton Commander | white, cyan and yellow on the PC's text-mode blue |
| 24 | AMIGA | the Amiga's Workbench | white and orange on blue |
| 25 | COBALT2 | Wes Bos's editor theme | white and orange on deep blue |

Every colour of the screens is in one file, `src/screen_fx/colors.h`. A
theme is one line of eleven colours there (`schemes`): add, change or drop
lines. The dark ones are all on pure black, in strong colours with their
captions and dim parts in the same strong hue rather than greyed down:
under a desk lamp, a dark or off-black theme looks flat on the backlit
panel. From 16 on they are light, or on a coloured background, with each
of their colours made darker or lighter (never another hue) until it
reads clearly: `tools/fx_preview.py --contrast` lists every theme's
contrast ratios (WCAG's: 4.5 is the usual bar for text, 7 the strict one)
and flags any that are low. Every
theme keeps green, amber and red for good, weak and lost (SITH's lost is
magenta), and has its own colours for a key holding a modifier and one
holding a layer. A light theme works too: its `bright` (a held key, the
newest of anything) is then its darkest colour. Under the themes are the colours no theme touches: Thermal's and the
screensavers', and those of the status text over them.
`tools/fx_preview.py --themes` draws all the themes on a computer.

#### Keys screen

| Part | Shows |
| --- | --- |
| top left, top right | the left and the right half: a battery and its percent ([amber, red and flashing](#battery-warnings) as it runs down; an empty battery and `--` when not known; while charging, the estimate, with an amber `~` before it), with a bolt to its left while the half is on USB power, signal strength as bars and in dBm, green, amber or red (`--` in red when the half isn't connected), and under them that signal over the last 40 seconds (kept whichever screen is showing, so it is there when Keys is turned to), a dim column a second with its top lit: a second of amber or red signal is a solid bar of that colour, so it stands out |
| top middle, on Bluetooth | the computer: the five Bluetooth profiles as on the Debug screen (filled: connected, ring: paired, dot: free; the selected one underlined), and the signal of the link to it, the same way as the halves' |
| top middle, over USB | `USB`, green when the computer is listening, amber when the computer has suspended the port (it is asleep), red otherwise; the typing speed in words a minute; and under it that speed over the last 40 seconds, its height 60 wpm or the fastest showing if that is more |
| the keys | every key position of the keyboard the dongle is built for; encoders as [knobs](#encoder-knobs) |
| under the keys | a level meter for each column of keys, as on a hi-fi: a key pressed in the column sends its bar up (and its neighbours' a little), more keys send it higher, to amber and red at the top, and it falls back in about two seconds. A white mark stays at the highest each bar reached for a moment, then falls after it |

Nothing on it moves as the readings change: each block has a place for the bolt, the battery with the signal bars under it, and the percent with the dBm under it, with room for `100%`; the typing speed has room for three figures.

##### Encoder knobs

On the Keys, Status and Debug screens, a key position that is an encoder's push is
drawn as a round knob instead of a square: on the Waterfowl both rollers and
both dials, on the Aurora Corne the outer thumb keys. Pushed, it fills like a
key (white, then glowing for two seconds). Turned, its ring glows for two seconds and
the mark on its rim turns with it, two and a half times as far (a click of a
Waterfowl encoder, 18 degrees, moves it an eighth of the way round), gliding there
over a few frames, the way the encoder went. The Debug log has ZMK's own
figure (`E0 +4`, in degrees as its encoder driver gives them), whose positive
is anticlockwise on the Waterfowl. Which
position is which encoder is `encoder_keys` in `src/dongle_screen_fx.c`, in the
order of the dongle overlay's `&sensors`, and `ENCODER_TURN_SIGN` there turns a
board's knobs round; a board missing from it has no knobs.
On the Debug screen the knobs are only 4 pixels across (3 on the Corne).

The battery levels and the output aren't drawn along the bottom of this
screen as they are over the animations: they are in the blocks at the top.
The histories start empty each time the screen is turned to.

Keys draws the keyboard from its matrix transform.
A key counts as holding a modifier or a layer if its binding is `&kp` with a
modifier, `&mo`, or a hold-tap over one of those (home row mods, `&lt`), and
only takes the colour while that modifier or layer is really active, so a
tapped home row mod stays white. Toggles and sticky keys aren't coloured.

Terminal shows the text itself, passwords included, to anyone who can see the
dongle; it only does so while it is the screen showing. It works the
characters out as a US layout would, so on a computer set to another layout
some symbols will differ from what the computer prints. Shortcuts (anything
with Ctrl, Alt or Cmd held) aren't shown.

Warp's comets are cyan for the left hand, amber for the right and magenta for
the thumbs. The animations place a key by taking the board as 10 columns by 4
rows, which is the Waterfowl; on the other boards it is only roughly right.

Every animation shows the batteries and the output along the bottom, in the
fonts of the module's Field screen, the levels a size up from its own
(`src/screen_fx/status_overlay.c`): the left half's level in the bottom left
corner, the right half's in the bottom right (`-` for a half that isn't
connected; [amber, red and flashing](#battery-warnings) as it runs down), and in the middle
the wireless symbol, with the Bluetooth profile number (counted from 0)
to its right when there is something to say:

| Output | Symbol | Profile number |
| --- | --- | --- |
| Bluetooth, computer connected | in the animation's colour | for 2 seconds after the profile is changed or the computer connects, then it fades away |
| Bluetooth, not connected | pulses grey | stays |
| USB | none, but for 2 seconds after the profile is changed (solid if that profile's computer is connected, grey if not), then it fades away | the same |

The text's colours are set per animation in `src/screen_fx/colors.h`
(Tracker's and Terminal's are the colour theme's): light with a dark edge.
The list at the end of `src/screen_fx/fx.c` (its `theme`) says which letters the numbers are in
and how low they sit: Terminal, Code, Thermal and Tracker have them in the
animations' own small pixel letters, a size down from the Field font; on
all of them, the screensavers too, they sit low in the bottom corners, clear
of the panel's rounding (on Tracker, under the line across the bottom); and Tracker
adds a gauge for each battery up the sides.

A half on USB power shows a bolt: to the left of its battery on the Keys
screen and to the right of its level on the Debug screen, neither moving
for it; over the animations and the screensavers, in the place of its level.
It flashes once a second while the level is under 100 and is solid at 100. While it charges ZMK's level reads high, from the
charging voltage, so the Keys, Status and Debug screens show the dongle's
estimate in its place, with an amber `~` (see [Debug screen](#debug-screen)):
that stops at 99, so there the bolt flashes until the half is unplugged.
Over the animations and on Tracker's gauge it is ZMK's, so take it as rough. Each
half says it is on USB power through
the Battery Level Status of Bluetooth's standard battery service
(`src/half_charging.c`). The dongle reads it once a half has connected and
ZMK has finished setting the link up, then subscribes (`src/dongle_half_info.c`),
so a half only sends it when it changes, in a connection event it wakes for
anyway: nothing is polled. Those reads go one at a time and wait for ZMK, as
ZMK can't wait for a free request while it subscribes to a half's keys: when
it couldn't, that half connected and showed its battery but typed nothing.
The Debug screen's `CH` column shows where it got to for each half. That is
all the dongle asks of the halves beyond what ZMK does: the temperature,
battery volts and transmit power it once also asked for never arrived, and
went. The bolt means
plugged in, not still charging: a half can't tell when its charger has
finished, so the solid bolt at 100 is only the reading reaching the top. A second after it is unplugged (and again at 10 and 30), every half reads
its battery again (`src/battery_refresh.c`), rather than at ZMK's next
once-a-minute reading, so the charger's level doesn't stay up for long. Until
that first reading the half still says it is on USB power, so its nice!view and
the dongle keep the bolt for that second, then both show the new level together
(the dongle gets the level first, then the bolt goes). A half on older firmware just
never shows the bolt. The screensavers show only that bolt, or a small
battery in the corner of a half at 15% or under, with the same colours and
flashing. The screen to start on
before one has been chosen and the typing speed that counts as flat out are set in
`config/prospector_adapter.conf`.

#### Battery warnings

Wherever the dongle shows a half's battery (the Keys, Status and Debug screens, the
levels over the animations, the screensavers' corner batteries):

| Level | Shown |
| --- | --- |
| 15% or under | amber |
| 10% or under | red, flashing slowly: a second on, a second off (not on Debug, which doesn't flash) |
| 5% or under | red, flashing fast: twice a second |

#### Status screen

Between Keys and Debug: more than Keys shows of the batteries and the
links, as bars and graphs, and none of Debug's raw codes. Laid out like a
racing car's steering wheel display.

| Part | Shows |
| --- | --- |
| the row of lights along the top | the typing speed, a light for every 8 words a minute: green, then red, then violet from 88 to 120. A white light stays at the fastest of the last 94 seconds |
| the graph under the lights | the typing speed over the last 94 seconds, a point a second, up to 120 at the top; the dotted lines are where the lights turn red (40) and violet (80). The dongle keeps this, and each link's signal, whichever screen is showing (since it started), so they are already there when the screen is turned to |
| left and right | each half of the board as on Keys (white while held, orange holding a modifier, violet holding a layer, glowing after; encoders as [knobs](#encoder-knobs)) |
| middle | the typing speed, big, with `WPM` under it |
| the three signals | each link's signal, the halves' over their batteries and the computer's between: its latest in dBm (the halves' at their outer edges), coloured as on Keys, `--` in red when down; while typing over USB the computer's isn't shown and the halves' widen to share its room; just under it the last 94 seconds, the weakest in each column, on a scale the graphs share from -100 dBm at the foot up to the strongest column shown, so the tallest reaches up to its reading and the links still compare; drawn as on Keys: a dim column with its top lit while above -75, a solid amber bar above -85 and a solid red one below, so weak seconds stand out, and a full-height red one where the link was down |
| bottom left, bottom right | each half's battery: its percent, big, coloured as [below](#battery-warnings) (while charging, the estimate, with an amber `~` over the `%`; `--` when not known); beside it, towards the middle, its battery in volts to the millivolt (dim while charging: it is the charger's then) and its average draw in mW (as on Debug: `--` for its first minute, then an amber `~` before it while rough, coloured by battery life once settled), their units between the halves; and under them a bar of ten, lit up to the level (to the estimate while charging), with a bolt at its inner end while the half is on USB power |
| the boxes, along the foot | the output: `BT` and the profile (green when the computer is connected) or `USB` (green when it is listening); `DR` the drops on all three links since the dongle started (amber when any); the dongle's supply in volts; its temperature (amber from 60 °C); then the worst trouble there is, filled red: `HANG` or `CRSH` the last start hung or crashed, `B` and milliseconds a switch bouncing (under 30 ms), `CH` a half's charging state failed; or amber `UE` and a count, USB reports the computer didn't take. `OK` in green when there is none. The Debug screen says more about each |

#### Debug screen

The first screen is a page of readings rather than an animation,
for chasing connection, switch and firmware faults. It is a first pass with
everything on it that the dongle can get at. Top to bottom:

| Line | Shows |
| --- | --- |
| `B149 1:12:46 ACT 10F 38MS` | the firmware: `B149` is CI build 149, the `build-149` release in Keyboard Writer (`DEV` if built anywhere else); time since the dongle started; `ACT`ive, `IDL`e or `SLP`; this screen's frames a second and the time one takes to draw and send |
| `RST SOFT 02  MEM 62% ^71` | why the dongle last restarted (`POR` power on, `PIN` reset button, `SOFT` asked to, e.g. into the bootloader; in red `WDOG` watchdog, `LOCK` CPU lock-up, `BRWN` brown-out) and the raw bits, or in red `HANG 1` when the last start showed no picture within 15 s (`HANG 2`: none within 15 s of waking from idle), so the dongle restarted itself, or `CRASH` and Zephyr's reason when it crashed and restarted (kept in a register through the restart, as the bootloader can wipe the RAM; a reason of 15 or more shows as `0F` then; after either it starts on the first animation, not the saved screen, and gives up after 3 in a row, lighting the backlight anyway); how full LVGL's memory is and the fullest it has been |
| `L`, `R`, `H` rows | the Bluetooth links to the left half, the right half and the computer (the active profile); the letter is red when the link is down |
| profile circles, `USB`, `RPT`, `E` | the five Bluetooth profiles (filled: connected, ring: paired, dot: free; the active one underlined); USB `--`, `PWR` powered or `HID` ready to type (green when typing over it); keyboard and media reports sent over USB since the dongle started (`12K` is 12,000) and how many failed, amber when any did: the computer didn't take them (its port asleep or busy). The output in use is in the `H` row |
| `L3 NAV 0009  ⌃⌥⇧⌘  51WPM` | the highest active layer and its name (shortened to fit), a bit for every active layer (hex); the modifiers as the Mac's symbols, lit while held (either side); typing speed (right-aligned, so the modifiers don't move as it changes) |
| `DRAW L1.42 R~0.9mW  31C` | each half's average draw in milliwatts, an amber `~` before it while rough, coloured by battery life once settled (below); at the right, the dongle's chip temperature in °C (amber from 60). The dongle's own readings (this and its supply in the `V` column) are read every 5 s, each on a thread of its own, so one that sticks blanks only itself. When one isn't known, amber and why: `WT` not read yet, `TO` a read 2 s in and not back (the temperature, after some flashes, until the dongle is unplugged), `NB` not built in; for the supply `E1` the ADC isn't ready, `E2` setting up its channel failed, `E3` the read failed, `E4` the result didn't convert; for the temperature `E1` the sensor isn't ready, `E2` measuring failed, `E3` getting the value failed |
| three event lines | the last raw events, newest at the bottom, each with the milliseconds since the one before: `P27v R` key position 27 down (`^` up) from the right half; `K7:08v 00` the keycode sent (HID usage page:id, then modifier bits); `E0 +1` encoder 0 turned |
| key map | every key position of the keyboard the dongle is built for, laid out by its matrix transform: white while held, glowing for two seconds after. A key that doesn't light isn't reaching the dongle. Encoders are [knobs](#encoder-knobs) |
| `BNC 18MS P23` | the shortest time any key has gone up and come back down, and which key; red under 30 ms, which no finger does: a bouncing switch |
| `WATERFOWL` | under the key map, lined up with it on the left: the keyboard the dongle is built for (moving left, then dropping words from the front, if the bounce reading leaves no room: `CORNE`) |

The link rows' columns:

| Column | Shows |
| --- | --- |
| `DBM` | how strongly the dongle hears the other end, in dBm: around -50 is strong, green above -75, amber to -85, red below. The radios still hear a packet at about -95, so green has 20 dB or more to spare (a hand or a body in the way can take 10 to 20), amber works but with less, and red is where packets start to go missing (sent again, so a key comes late) and then the link drops. (The Keys screen draws it as bars, one for every 10 dB above -90.) Blank, or `E` and a code in red, when the radio wouldn't say: `EH` no handle for the link, `EB` no buffer for the question, a number Zephyr's error (`E5`: the radio refused) |
| `%` | the half's battery percent, as ZMK reports it, or while it charges the estimate, with an amber `~` before it (below); with a bolt beside it while the half is on USB power, flashing until 100, so the level can be watched going up against `V`. For the computer, the output (`BT1`, `USB`) |
| `V` | the half's battery in volts, to the millivolt (`3.912`), the average of ZMK's last 16 readings (a minute apart), sent when it moves by 1 mV (dim while charging: it is the charger's then; `--` for a half whose firmware doesn't send it). On `H`, the dongle's own supply, its 3.3 V regulator's output (the USB 5 V is only detected: nothing on the board can measure it) |
| `DR` | times the link has dropped since the dongle started |
| `RS` | Bluetooth's reason for the last drop, in hex: `08` the other end went silent (out of range, flat battery, switched off), `13` the other end closed it, `16` the dongle closed it, `3E` it never got going |
| `CH` | for a half (blank on `H`), where the dongle has got with its charging state (`src/dongle_half_info.c`): `OK` (green) subscribed, so a bolt shows in place of the level while it is plugged in, and its volts come; on the way, in amber, `WT` waiting for ZMK to finish setting the link up, `Q1` queued behind the other half or waiting to try again, `R1` reading, `S1` subscribing (the figure is the try); `NF` the half's firmware doesn't have it; in red `E` and the last error in hex (an ATT error code, or from `80` a Zephyr errno plus `80`) after 5 tries, or `TO` a request with no answer in 10 s, left until the half next connects |
| `KEY` | key presses from that half since the dongle started; on `H`, both halves together. Shortened past 999: `1.2K`, `12K`, `1.2M` |

The `mW` figures are each half's average draw, to compare boards and
settings (`src/dongle_half_drain.c`). Each half sends its battery voltage in
the same message as its charging state: the average of ZMK's last 16
readings, a minute apart (started again when USB power comes or goes), when
it has moved by 1 mV. One reading jitters by a few mV and dips while the
radio sends; the average doesn't. Once a minute the dongle turns it into the energy
left, from a typical LiPo cell's curve and the battery's capacity
(`CONFIG_HALF_BATTERY_MAH` in each `config/<keyboard>.conf`: Waterfowl, Urchin
and Aurora Corne 200, Forager 120 mAh), and fits a straight line
through those since the half was last unplugged (or the dongle started). It
is `--` until it has two readings a minute apart. After that it shows, with a
amber `~` in front and one place fewer while it is rough: while it
may be out by more than a quarter, from the line's scatter or one 1 mV step
over the time the line spans, whichever is more, and while the half is still
settling. After unplugging (and when the dongle starts) the voltage comes
down from the charger's much faster than any half draws, so while it falls
8 mV or more in 20 minutes the line starts again from there; it has settled
once it falls less, and starts again once more then. It is an estimate: the voltage moves only a few
millivolts for each percent through the middle of the charge, so at a
milliwatt or so it can stay rough for some hours (halves on firmware from
before the average send 4 mV steps, and stay rough for a day or more). Best for comparing halves over a day or more. Once
settled, it is coloured by how long a full battery would last at it: green
four weeks or more (under about 1.1 mW on 200 mAh, 0.65 mW on the Forager's
120), plain a week to four, amber under a week (over about 4.4 mW, 2.6 mW),
red under two days (over about 15 mW, 9 mW).

While a half charges, ZMK's level is the charger's voltage, near 100
whatever the charge, so the dongle estimates it instead (in
`src/dongle_half_drain.c` too): from ZMK's level at the last reading before
the half was plugged in, on battery and so a true one, it adds the charge
current for the time since as a share of the capacity
(`CONFIG_HALF_CHARGE_MA`, next to the capacity: 50 mA on the Forager, its
XIAO's charger left at its default; 100 mA on the nice!nano v2 boards), at
half that above 80%, where a cell's charger eases off, and no more than 99
until it is unplugged, as a half can't say when its charger has finished.
After unplugging the estimate stays as it was until the voltage has settled
(as for the draw), then ZMK's level is right again; plugged in again before
that, it carries on from there. When nothing was heard from the half before
it was plugged in (the dongle started while it charged) it is `--`.

Key timings are taken when an event reaches the
dongle, and a half sends at most once per connection interval, so a bounce
shorter than that (7.5 ms) can't be seen from here. The counts and the draw start again
when the dongle restarts.

Only the dongle needs flashing to change these (the keymap and the screen
code both live there), except the charging state and battery volts, which the halves' own firmware sends: pick the build in
[Keyboard Writer](https://github.com/russwinch/keyboard-writer), see
[Flashing](#flashing).

The drawing code (`src/screen_fx/fx.c`) has no firmware in it, so
`tools/fx_preview.py` can run it on a computer and save each animation as a
GIF in `fx_preview_out/` (needs gcc, git and Pillow): the quick way to try a
change or a new animation. The status text is drawn as on the dongle; its
fonts are fetched from the Prospector module the first time. The panel's
corners are rounded off (about 43 of its pixels, `FX_CORNER_R` in
`src/screen_fx/fx.h`, which `fx_corner_inset` keeps text clear of), and the
GIFs black them out the same way; `--square` shows the whole picture.
`--board waterfowl` (or `corne`) draws Keys and Debug on that board,
encoders turning, instead of the Forager. `tools/fx_preview.py --saver thermal` saves each screensaver coming over that
animation and going again, and `--power keys` the panel going off and
coming back on. Only one animation can run at a time (they share
their memory), so the other picture in a transition is a still: the last
frame sent, which the dongle keeps anyway, and its palette. A transition
changes every pixel, so it runs at the whole-frame rate or under. On the dongle, `src/dongle_screen_fx.c` parks LVGL on an
empty screen and writes the frames straight to the panel, a quarter of the
pixels doubled up. The dongle has no RAM to spare for the pictures, so they
are kept in one of LVGL's drawing buffers, idle while its screen is parked.
The SPI link to the panel sets the frame rate: a whole
frame takes about 70 ms, so full-screen animations run at around 15 a second.

If one peripheral stops
sending keys after connecting to the dongle, reset that half (a known ZMK issue,
[zmk#3156](https://github.com/zmkfirmware/zmk/issues/3156)).
To go back, flash `settings_reset` again and then the normal `waterfowl_left`.

## Adding a keyboard

1. Add its modules (if any) to `config/west.yml` and its halves to `build.yaml`.
2. Create `config/<shield>.keymap` that defines `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see `splitkb_aurora_corne.keymap`
   for one with extra keys and encoders), then `#include "keymap.dtsi"`.
3. Add a row to the tables above.
