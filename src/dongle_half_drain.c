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
 * typical one, and through the middle of the charge the voltage moves only
 * a few millivolts for each percent. So nothing shows until it has an hour
 * of readings, and it is steadier the longer it runs. For the same reason
 * the first SETTLE_MIN after unplugging aren't used: the voltage is still
 * coming down from the charger's.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include "dongle_half_info.h"

#define SAMPLE_EVERY_S 60
#define SETTLE_MIN 20       // after unplugging, before readings count
#define SHOW_AFTER_MIN 60   // of readings, before there is a figure
#define NOMINAL_MV 3700     // to turn mAh into mWh

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

// A least-squares line through (hours, mWh), as running sums.
struct fit {
    uint32_t n;
    uint32_t start_ms, last_ms; // uptime of the first and last readings
    double t, e, tt, te;
};

static struct fit fits[2];          // under lock
static uint32_t settle_until[2];    // uptime ms: readings count from then
static bool was_charging[2];
static struct k_spinlock lock;

static void sample_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_cb);

static void sample_cb(struct k_work *work) {
    const uint32_t now = k_uptime_get_32();
    for (int half = 0; half < 2; half++) {
        const bool charging = dongle_half_charging(half);
        const uint16_t mv = dongle_half_millivolts(half);
        K_SPINLOCK(&lock) {
            if (charging) {
                fits[half] = (struct fit){0}; // starts again once unplugged
                was_charging[half] = true;
                K_SPINLOCK_BREAK;
            }
            if (was_charging[half] && mv != 0) {
                // unplugged (heard as a voltage with no charging)
                was_charging[half] = false;
                settle_until[half] = now + SETTLE_MIN * 60 * 1000;
            }
            if (mv == 0 || (int32_t)(now - settle_until[half]) < 0) {
                K_SPINLOCK_BREAK; // not connected, or settling
            }
            struct fit *f = &fits[half];
            if (f->n == 0) {
                f->start_ms = now;
            }
            const double t = (now - f->start_ms) / 3600000.0;
            const double e = energy_mwh(mv);
            f->n++;
            f->last_ms = now;
            f->t += t;
            f->e += e;
            f->tt += t * t;
            f->te += t * e;
        }
    }
    k_work_schedule(&sample_work, K_SECONDS(SAMPLE_EVERY_S));
}

int dongle_half_drain_hundredths_mw(int half) {
    if (half < 0 || half >= 2) {
        return -1;
    }
    int hundredths = -1;
    K_SPINLOCK(&lock) {
        const struct fit *f = &fits[half];
        if (f->n < 2 || f->last_ms - f->start_ms < SHOW_AFTER_MIN * 60 * 1000) {
            K_SPINLOCK_BREAK;
        }
        const double denom = f->n * f->tt - f->t * f->t;
        if (denom <= 0) {
            K_SPINLOCK_BREAK;
        }
        const double mw = -(f->n * f->te - f->t * f->e) / denom; // mWh lost an hour
        hundredths = (int)CLAMP(mw * 100 + 0.5, 0, 99999);
    }
    return hundredths;
}

static int dongle_half_drain_init(void) {
    k_work_schedule(&sample_work, K_SECONDS(SAMPLE_EVERY_S));
    return 0;
}

SYS_INIT(dongle_half_drain_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
