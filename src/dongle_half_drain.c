/*
 * Each half's average draw in milliwatts, for the dongle's Debug screen, to
 * compare boards and settings.
 *
 * From the battery's voltage, which each half sends (src/half_charging.c,
 * src/dongle_half_info.c): once a minute the dongle turns the last voltage
 * heard into the charge left in the battery, from a typical lithium polymer
 * cell's resting voltage at each level of charge and the battery's capacity
 * (CONFIG_HALF_BATTERY_MAH, per keyboard). The current is the slope of a
 * straight line fitted through all of those since the half was last
 * unplugged (or the dongle started), so it averages out the reading's noise;
 * times the battery's voltage now, that is the draw in mW.
 *
 * It is an estimate: the cell's real curve differs a little from the
 * typical one, through the middle of the charge the voltage moves only a
 * few millivolts for each percent, and a half sends it (an average of its
 * last readings, whole millivolts) only when it has moved
 * by a millivolt. So it is steadier the longer it runs. It shows after
 * SHOW_AFTER_MIN of readings, and is rough (the screens flash a ~ before it)
 * while it may be out by more than ROUGH_PERCENT: by the line's scatter, or
 * by one STEP_TENTHS over the time it spans, whichever is more.
 *
 * After unplugging (and when the dongle starts, not knowing) the voltage is
 * still coming down from the charger's, much faster than any half draws. So
 * while it has fallen by SETTLE_FALL_MV or more in the last SETTLE_MIN the
 * line starts again from there, and the figure is rough; it is settled once
 * it falls less than that, and the line starts once more from then.
 *
 * While a half charges its level reads high (ZMK's is from the charger's
 * voltage), so the screens show an estimate instead, from ZMK's level at
 * the last reading before it was plugged in, on battery and so a true one:
 * CONFIG_HALF_CHARGE_MA for the time since, as a share of the capacity, at
 * half that above TAPER_PERCENT, where a cell's charger eases off, and no
 * more than 99 until it is unplugged, as the half can't say whether its
 * charger has finished. Once unplugged the estimate stays as it was until
 * the voltage has settled, then ZMK's level is right again. Plugged in again
 * before then, it carries on from there. Nothing heard before it was plugged
 * in (the dongle started while it charged): not known.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <math.h>

#include <zmk/split/central.h>

#include "dongle_half_info.h"

#define SAMPLE_EVERY_S 60
#define SHOW_AFTER_MIN 1    // of readings, before there is a figure
#define ROUGH_PERCENT 25    // rough while it may be out by more than this
#define STEP_TENTHS 10      // a half sends its voltage when it moves by this, 1 mV (src/half_charging.c)
#define SETTLE_MIN 20       // settling while it falls SETTLE_FALL_MV or more in this
#define SETTLE_FALL_MV 8
#define TREND_N (SETTLE_MIN + 1) // the readings back to SETTLE_MIN ago
#define TAPER_PERCENT 80    // charging at half the rate above this

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

// Charge left at `tenths` of a millivolt, in mAh.
static double charge_mah(uint32_t tenths) {
    const double mv = tenths / 10.0;
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
    return percent / 100.0 * CONFIG_HALF_BATTERY_MAH;
}

// A least-squares line through (hours, mAh from the first), as running sums.
struct fit {
    uint32_t n;
    uint32_t start_ms, last_ms; // uptime of the first and last readings
    double e0;                  // the first reading's mAh
    double t, e, tt, te, ee;
    uint32_t last_tenths;       // the last reading, 0.1 mV
};

static struct fit fits[2];          // under lock
static bool settling[2] = {true, true}; // until the voltage stops coming down from the charger's
static bool was_charging[2];
static uint16_t trend[2][TREND_N]; // under lock: a reading a minute (0.1 mV), trend_at the next to write
static int trend_at;
static struct k_spinlock lock;
// under lock: the charging estimate
static uint8_t rest_level[2];  // ZMK's level at the last reading on battery; 0 not known
static bool estimating[2];     // from plugging in until the voltage has settled after
static uint8_t start_level[2]; // the level it started charging from; 0 not known
static uint32_t plugged_ms[2]; // uptime then
static uint8_t held_level[2];  // and once unplugged, the estimate then

// The level after charging from `from` for `ms`.
static uint8_t charged_level(uint8_t from, uint32_t ms) {
    double percent = from + ms / 3600000.0 * CONFIG_HALF_CHARGE_MA * 100 / CONFIG_HALF_BATTERY_MAH;
    const double taper_from = MAX(from, TAPER_PERCENT);
    if (percent > taper_from) {
        percent = taper_from + (percent - taper_from) / 2;
    }
    return (uint8_t)MIN(percent + 0.5, 99);
}

// The estimate now (0 not known); under lock, while estimating.
static uint8_t estimate_now(int half, uint32_t now) {
    if (!was_charging[half]) {
        return held_level[half];
    }
    return start_level[half] ? charged_level(start_level[half], now - plugged_ms[half]) : 0;
}

static void sample_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_cb);

static void sample_cb(struct k_work *work) {
    const uint32_t now = k_uptime_get_32();
    for (int half = 0; half < 2; half++) {
        const bool charging = dongle_half_charging(half);
        const uint32_t tenths = dongle_half_tenths_mv(half); // of a millivolt
        uint8_t level = 0;
        if (half < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT) {
            zmk_split_central_get_peripheral_battery_level(half, &level);
        }
        K_SPINLOCK(&lock) {
            trend[half][trend_at] = (uint16_t)MIN(tenths, UINT16_MAX);
            if (charging && !was_charging[half]) {
                // just plugged in: from the last level on battery, or where it had got to;
                // with neither (plugged in before the dongle had a level on battery),
                // ZMK's now, before the charger has had long to lift it
                start_level[half] = estimating[half] ? held_level[half] : rest_level[half];
                start_level[half] = start_level[half] ? start_level[half] : level;
                plugged_ms[half] = now;
                estimating[half] = true;
            }
            if (charging) {
                fits[half] = (struct fit){0}; // starts again once unplugged
                was_charging[half] = true;
                K_SPINLOCK_BREAK;
            }
            if (was_charging[half] && tenths != 0) {
                // unplugged (heard as a voltage with no charging)
                held_level[half] = estimate_now(half, now);
                was_charging[half] = false;
                settling[half] = true;
            }
            if (tenths == 0) {
                K_SPINLOCK_BREAK; // not connected
            }
            struct fit *f = &fits[half];
            if (settling[half]) {
                // still coming down from the charger's, or just settled: start again from here
                const uint16_t before = trend[half][(trend_at - SETTLE_MIN + TREND_N) % TREND_N];
                if (before != 0) {
                    settling[half] = before >= tenths + SETTLE_FALL_MV * 10;
                    *f = (struct fit){0};
                }
            }
            if (!settling[half]) {
                estimating[half] = false; // ZMK's level is right again
            }
            if (!estimating[half] && level) {
                rest_level[half] = level;
            }
            if (f->n == 0) {
                f->start_ms = now;
                f->e0 = charge_mah(tenths);
            }
            const double t = (now - f->start_ms) / 3600000.0;
            const double e = charge_mah(tenths) - f->e0;
            f->n++;
            f->last_ms = now;
            f->last_tenths = tenths;
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

int dongle_half_drain_hundredths_mw(int half, bool *rough) {
    *rough = true;
    if (half < 0 || half >= 2) {
        return -1;
    }
    int hundredths = -1;
    K_SPINLOCK(&lock) {
        const struct fit *f = &fits[half];
        if (f->n < 2 || f->last_ms - f->start_ms < SHOW_AFTER_MIN * 60 * 1000) {
            K_SPINLOCK_BREAK;
        }
        const double sxx = f->tt - f->t * f->t / f->n; // hours²
        if (sxx <= 0) {
            K_SPINLOCK_BREAK;
        }
        const double sxy = f->te - f->t * f->e / f->n;
        const double syy = f->ee - f->e * f->e / f->n;
        const double ma = -sxy / sxx; // mAh lost an hour
        const double mw = ma * f->last_tenths / 10000.0; // at the battery's voltage now
        hundredths = (int)CLAMP(mw * 100 + 0.5, 0, 99999);

        // how far out it may be: the line's scatter, or one step over its span
        const double sse = MAX(syy - sxy * sxy / sxx, 0);
        const double scatter = f->n > 2 ? sqrt(sse / (f->n - 2) / sxx) : INFINITY;
        const double span_h = (f->last_ms - f->start_ms) / 3600000.0;
        const double step = (charge_mah(f->last_tenths) - charge_mah(f->last_tenths - STEP_TENTHS)) / span_h;
        *rough = settling[half] || MAX(scatter, step) * 100 > ROUGH_PERCENT * ma;
    }
    return hundredths;
}

uint8_t dongle_half_charge_estimate(int half, bool *estimate) {
    *estimate = false;
    if (half < 0 || half >= 2) {
        return 0;
    }
    uint8_t level = 0;
    K_SPINLOCK(&lock) {
        *estimate = estimating[half];
        if (*estimate) {
            level = estimate_now(half, k_uptime_get_32());
        }
    }
    /* no level from before it was plugged in (say the dongle started with
     * the half already charging): no estimate, so the screens show ZMK's
     * own rather than nothing */
    *estimate = *estimate && level != 0;
    return level;
}

static int dongle_half_drain_init(void) {
    k_work_schedule(&sample_work, K_SECONDS(SAMPLE_EVERY_S));
    return 0;
}

SYS_INIT(dongle_half_drain_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
