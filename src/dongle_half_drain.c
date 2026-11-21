/*
 * Each half's average draw in milliwatts, for the dongle's Debug screen, to
 * compare boards and settings.
 *
 * From the battery's voltage, which each half sends (src/half_charging.c,
 * src/dongle_half_info.c): once a minute the dongle turns the last voltage
 * heard into the energy left in the battery, from a typical lithium polymer
 * cell's resting voltage at each level of charge and the battery's capacity
 * (CONFIG_HALF_BATTERY_MAH, per keyboard, at a nominal 3.7 V). The draw is
 * the slope of a straight line fitted through all of those since the half
 * was last unplugged (or the dongle started), so it averages out the
 * reading's noise and the dips while typing.
 *
 * It is an estimate: the cell's real curve differs a little from the
 * typical one, through the middle of the charge the voltage moves only a
 * few millivolts for each percent, and a half sends it only when it has
 * moved by STEP_MV. So it is steadier the longer it runs. It shows after
 * SHOW_AFTER_MIN of readings, and is rough (the screens flash a ~ before it)
 * while it may be out by more than ROUGH_PERCENT: by the line's scatter, or
 * by one STEP_MV over the time it spans, whichever is more.
 *
 * After unplugging (and when the dongle starts, not knowing) the voltage is
 * still coming down from the charger's, much faster than any half draws. So
 * while it has fallen by SETTLE_FALL_MV or more in the last SETTLE_MIN the
 * line starts again from there, and the figure is rough; it is settled once
 * it falls less than that.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <math.h>

#include "dongle_half_info.h"

#define SAMPLE_EVERY_S 60
#define SHOW_AFTER_MIN 15   // of readings, before there is a figure
#define ROUGH_PERCENT 25    // rough while it may be out by more than this
#define STEP_MV 4           // a half sends its voltage when it moves by this (src/half_charging.c)
#define SETTLE_MIN 20       // settling while it falls SETTLE_FALL_MV or more in this
#define SETTLE_FALL_MV 8
#define NOMINAL_MV 3700     // to turn mAh into mWh
#define TREND_N 240         // the last four hours of readings, for the Status screen

// A typical lithium polymer cell resting: millivolts, and the percent of its
// charge left there. Above the first, full; below the last, empty.
static const struct {
    uint16_t mv;
    uint8_t percent;
} curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
    {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
    {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3270, 0},
};

// Energy left at `mv`, in mWh.
static double energy_mwh(uint16_t mv) {
    double percent = 0;
    if (mv >= curve[0].mv) {
        percent = 100;
    } else {
        for (size_t i = 1; i < ARRAY_SIZE(curve); i++) {
            if (mv >= curve[i].mv) {
                const double span = curve[i - 1].mv - curve[i].mv;
                percent = curve[i].percent + (curve[i - 1].percent - curve[i].percent) * (mv - curve[i].mv) / span;
                break;
            }
        }
    }
    return percent / 100.0 * CONFIG_HALF_BATTERY_MAH * NOMINAL_MV / 1000.0;
}

// A least-squares line through (hours, mWh from the first), as running sums.
struct fit {
    uint32_t n;
    uint32_t start_ms, last_ms; // uptime of the first and last readings
    double e0;                  // the first reading's mWh
    double t, e, tt, te, ee;
    uint16_t last_mv;
};

static struct fit fits[2];          // under lock
static bool settling[2] = {true, true}; // until the voltage stops coming down from the charger's
static bool was_charging[2];
static uint16_t trend[2][TREND_N]; // under lock: a reading a minute, trend_at the next to write
static int trend_at;
static struct k_spinlock lock;

static void sample_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_cb);

static void sample_cb(struct k_work *work) {
    const uint32_t now = k_uptime_get_32();
    for (int half = 0; half < 2; half++) {
        const bool charging = dongle_half_charging(half);
        const uint16_t mv = dongle_half_millivolts(half);
        K_SPINLOCK(&lock) {
            trend[half][trend_at] = mv;
            if (charging) {
                fits[half] = (struct fit){0}; // starts again once unplugged
                was_charging[half] = true;
                K_SPINLOCK_BREAK;
            }
            if (was_charging[half] && mv != 0) {
                // unplugged (heard as a voltage with no charging)
                was_charging[half] = false;
                settling[half] = true;
            }
            if (mv == 0) {
                K_SPINLOCK_BREAK; // not connected
            }
            struct fit *f = &fits[half];
            if (settling[half]) {
                // still coming down from the charger's: start again from here
                const uint16_t before = trend[half][(trend_at - SETTLE_MIN + TREND_N) % TREND_N];
                if (before != 0 && before < mv + SETTLE_FALL_MV) {
                    settling[half] = false;
                } else if (before != 0) {
                    *f = (struct fit){0};
                }
            }
            if (f->n == 0) {
                f->start_ms = now;
                f->e0 = energy_mwh(mv);
            }
            const double t = (now - f->start_ms) / 3600000.0;
            const double e = energy_mwh(mv) - f->e0;
            f->n++;
            f->last_ms = now;
            f->last_mv = mv;
            f->t += t;
            f->e += e;
            f->tt += t * t;
            f->te += t * e;
            f->ee += e * e;
        }
    }
    K_SPINLOCK(&lock) {
        trend_at = (trend_at + 1) % TREND_N;
    }
    k_work_schedule(&sample_work, K_SECONDS(SAMPLE_EVERY_S));
}

void dongle_half_trend(int half, uint16_t *mv, int n, int every_min) {
    K_SPINLOCK(&lock) {
        for (int i = 0; i < n; i++) {
            // the newest is the one before trend_at; each before it every_min older
            const int ago = (n - 1 - i) * every_min + 1;
            mv[i] = half >= 0 && half < 2 && ago <= TREND_N ? trend[half][(trend_at - ago + TREND_N) % TREND_N] : 0;
        }
    }
}

int dongle_half_drain_hundredths_mw(int half, bool *rough) {
    *rough = true;
    if (half < 0 || half >= 2) {
        return -1;
    }
    int hundredths = -1;
    K_SPINLOCK(&lock) {
        const struct fit *f = &fits[half];
        if (f->n < 3 || f->last_ms - f->start_ms < SHOW_AFTER_MIN * 60 * 1000) {
            K_SPINLOCK_BREAK;
        }
        const double sxx = f->tt - f->t * f->t / f->n; // hours²
        if (sxx <= 0) {
            K_SPINLOCK_BREAK;
        }
        const double sxy = f->te - f->t * f->e / f->n;
        const double syy = f->ee - f->e * f->e / f->n;
        const double mw = -sxy / sxx; // mWh lost an hour
        hundredths = (int)CLAMP(mw * 100 + 0.5, 0, 99999);

        // how far out it may be: the line's scatter, or one step over its span
        const double sse = MAX(syy - sxy * sxy / sxx, 0);
        const double scatter = sqrt(sse / (f->n - 2) / sxx);
        const double span_h = (f->last_ms - f->start_ms) / 3600000.0;
        const double step = (energy_mwh(f->last_mv + STEP_MV / 2) - energy_mwh(f->last_mv - STEP_MV / 2)) / span_h;
        *rough = settling[half] || MAX(scatter, step) * 100 > ROUGH_PERCENT * mw;
    }
    return hundredths;
}

static int dongle_half_drain_init(void) {
    k_work_schedule(&sample_work, K_SECONDS(SAMPLE_EVERY_S));
    return 0;
}

SYS_INIT(dongle_half_drain_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
