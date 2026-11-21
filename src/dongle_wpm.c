/*
 * Prospector dongle: the typing speed, worked out here in place of ZMK's.
 *
 * ZMK's (app/src/wpm.c) counts every key code released, Shift, Cmd and
 * Backspace among them, and starts again every 5 seconds, so its figure
 * jumps (one second of typing taken for a minute) and reads well over a
 * typing test's. This one counts what a typing test does: the characters
 * typed (src/dongle_screen_fx.c's typed_char: letters, figures, symbols,
 * space, Tab and Enter), less one for each Backspace, over the last
 * WINDOW_S seconds, five to a word. Starting from rest, the window is only
 * as long as the typing has been going (MIN_WINDOW_S at least), so the
 * figure comes up quickly without the first second's jump; when the typing
 * stops it falls away over WINDOW_S.
 *
 * Everything that reads the speed gets this one: the link step wraps ZMK's
 * zmk_wpm_get_state and raise_zmk_wpm_state_changed (-Wl,--wrap in
 * CMakeLists.txt), so ZMK's own count still runs but goes nowhere, and the
 * speed event, which the animated screens and the Prospector module's own
 * screens follow, carries this figure.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>

#include <zmk/events/wpm_state_changed.h>

#include "dongle_wpm.h"

#define WINDOW_S 10
#define MIN_WINDOW_S 3

int __real_raise_zmk_wpm_state_changed(struct zmk_wpm_state_changed ev);

static atomic_t this_second; /* characters, less backspaces, since the last tick */
static atomic_t speed;
static int16_t per_second[WINDOW_S]; /* the last WINDOW_S seconds' counts: the tick's only */
static int next, typing_for, quiet_for;

void dongle_wpm_typed(char c) { atomic_add(&this_second, c == '\b' ? -1 : 1); }

static void tick_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick_cb);

static void tick_cb(struct k_work *work) {
    k_work_schedule(&tick_work, K_SECONDS(1));

    const int n = (int)atomic_set(&this_second, 0);
    per_second[next] = (int16_t)CLAMP(n, INT16_MIN, INT16_MAX);
    next = (next + 1) % WINDOW_S;

    /* how long the typing has been going: from rest, the first second with any */
    quiet_for = n != 0 ? 0 : MIN(quiet_for + 1, WINDOW_S);
    typing_for = quiet_for >= WINDOW_S ? 0 : typing_for > 0 || n != 0 ? MIN(typing_for + 1, WINDOW_S) : 0;

    int typed = 0;
    for (int i = 0; i < WINDOW_S; i++) {
        typed += per_second[i];
    }
    const int window = CLAMP(typing_for, MIN_WINDOW_S, WINDOW_S);
    const int wpm = typed <= 0 ? 0 : MIN(typed * 60 / 5 / window, 255);
    if (atomic_set(&speed, wpm) != wpm) {
        __real_raise_zmk_wpm_state_changed((struct zmk_wpm_state_changed){.state = wpm});
    }
}

/* ZMK's own figure: dropped */
int __wrap_raise_zmk_wpm_state_changed(struct zmk_wpm_state_changed ev) {
    ARG_UNUSED(ev);
    return 0;
}

int __wrap_zmk_wpm_get_state(void) { return (int)atomic_get(&speed); }

static int dongle_wpm_init(void) {
    k_work_schedule(&tick_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(dongle_wpm_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
