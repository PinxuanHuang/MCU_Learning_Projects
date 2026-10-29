/* M2.2 - XPT2046 5-point calibration for the 320x240 landscape TFT.
 * LCD rotation is already 3; this code determines axes/flip/skew from samples.
 * No flash writes, no TFT DMA overlap and no Dashboard gesture injection.
 */
#include "touch_calibration.h"
#include "touch_raw.h"
#include "ili9341.h"
#include "main.h"
#include <stddef.h>

#define TOUCH_CAL_POINT_TIMEOUT_MS 12000U
#define TOUCH_CAL_MIN_SAMPLES 6U
#define TOUCH_CAL_MAX_SAMPLES 8U
#define TOUCH_CAL_MAX_RAW_JITTER 230U
#define TOUCH_CAL_MAX_RESIDUAL_SQ 324.0 /* 18-pixel max fitting error */

TouchCalibration touch_calibration;
volatile TouchCalDebug touch_cal_debug;

static double absd(double value) { return value < 0.0 ? -value : value; }

/* Solve a 3x3 normal-equation system using partial-pivot elimination.
 * This handles raw X/Y axis swaps, flips, translation, and slight skew.
 */
static uint8_t solve3(const double a[3][3], const double b[3], double x[3])
{
    double m[3][4];
    for (uint8_t i = 0U; i < 3U; ++i) {
        for (uint8_t j = 0U; j < 3U; ++j) m[i][j] = a[i][j];
        m[i][3] = b[i];
    }
    for (uint8_t c = 0U; c < 3U; ++c) {
        uint8_t pivot = c;
        for (uint8_t r = (uint8_t)(c + 1U); r < 3U; ++r)
            if (absd(m[r][c]) > absd(m[pivot][c])) pivot = r;
        if (absd(m[pivot][c]) < 1e-7) return 0U;
        if (pivot != c) {
            for (uint8_t j = c; j < 4U; ++j) {
                double tmp = m[c][j]; m[c][j] = m[pivot][j]; m[pivot][j] = tmp;
            }
        }
        for (uint8_t r = (uint8_t)(c + 1U); r < 3U; ++r) {
            double factor = m[r][c] / m[c][c];
            for (uint8_t j = c; j < 4U; ++j) m[r][j] -= factor * m[c][j];
        }
    }
    for (int8_t i = 2; i >= 0; --i) {
        double value = m[(uint8_t)i][3];
        for (uint8_t j = (uint8_t)(i + 1); j < 3U; ++j)
            value -= m[(uint8_t)i][j] * x[j];
        x[(uint8_t)i] = value / m[(uint8_t)i][(uint8_t)i];
    }
    return 1U;
}

uint8_t TouchCal_SolveFive(const TouchCalPoint p[TOUCH_CAL_POINT_COUNT],
                         TouchCalibration *out, uint16_t *max_error_px)
{
    if (p == NULL || out == NULL) return 0U;
    *out = (TouchCalibration){0};
    if (max_error_px != NULL) *max_error_px = 0U;
    uint16_t minx = 4095U, miny = 4095U, maxx = 0U, maxy = 0U;
    double a[3][3] = {{0}};
    double bx[3] = {0}, by[3] = {0};
    for (uint8_t i = 0U; i < TOUCH_CAL_POINT_COUNT; ++i) {
        if (p[i].raw_x > 4095U || p[i].raw_y > 4095U ||
            p[i].screen_x >= TOUCH_CAL_WIDTH || p[i].screen_y >= TOUCH_CAL_HEIGHT)
            return 0U;
        if (p[i].raw_x < minx) minx = p[i].raw_x;
        if (p[i].raw_y < miny) miny = p[i].raw_y;
        if (p[i].raw_x > maxx) maxx = p[i].raw_x;
        if (p[i].raw_y > maxy) maxy = p[i].raw_y;
        double v[3] = {(double)p[i].raw_x, (double)p[i].raw_y, 1.0};
        for (uint8_t r = 0U; r < 3U; ++r) {
            bx[r] += v[r] * (double)p[i].screen_x;
            by[r] += v[r] * (double)p[i].screen_y;
            for (uint8_t c = 0U; c < 3U; ++c) a[r][c] += v[r] * v[c];
        }
    }
    /* Reject missing X/Y travel.  This also catches an unwired/stuck ADC. */
    if ((uint16_t)(maxx - minx) < 350U ||
        (uint16_t)(maxy - miny) < 350U) return 0U;
    double x[3] = {0}, y[3] = {0};
    if (!solve3(a, bx, x) || !solve3(a, by, y)) return 0U;
    double worst = 0.0;
    for (uint8_t i = 0U; i < TOUCH_CAL_POINT_COUNT; ++i) {
        double px = x[0]*p[i].raw_x + x[1]*p[i].raw_y + x[2];
        double py = y[0]*p[i].raw_x + y[1]*p[i].raw_y + y[2];
        double dx = px - p[i].screen_x;
        double dy = py - p[i].screen_y;
        double sq = dx*dx + dy*dy;
        if (sq > TOUCH_CAL_MAX_RESIDUAL_SQ) return 0U;
        if (sq > worst) worst = sq;
    }
    uint16_t error_ceil = 0U;
    while ((double)error_ceil * (double)error_ceil < worst && error_ceil < 19U)
        error_ceil++;
    if (max_error_px != NULL) *max_error_px = error_ceil;
    *out = (TouchCalibration){x[0], x[1], x[2], y[0], y[1], y[2], 1U};
    return 1U;
}

uint8_t TouchCal_Map(const TouchCalibration *cal, uint16_t raw_x,
                   uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y)
{
    if (cal == NULL || !cal->valid || screen_x == NULL || screen_y == NULL ||
        raw_x > 4095U || raw_y > 4095U) return 0U;
    double px = cal->a * raw_x + cal->b * raw_y + cal->c;
    double py = cal->d * raw_x + cal->e * raw_y + cal->f;
    /* Prevent bad coefficients from overflowing the integer cast. */
    if (px < -512.0 || px > 832.0 || py < -512.0 || py > 752.0)
        return 0U;
    if (px < 0.0) px = 0.0;
    if (py < 0.0) py = 0.0;
    if (px > (double)(TOUCH_CAL_WIDTH - 1U)) px = (double)(TOUCH_CAL_WIDTH - 1U);
    if (py > (double)(TOUCH_CAL_HEIGHT - 1U)) py = (double)(TOUCH_CAL_HEIGHT - 1U);
    *screen_x = (uint16_t)(px + 0.5);
    *screen_y = (uint16_t)(py + 0.5);
    return 1U;
}

static const TouchCalPoint s_targets[TOUCH_CAL_POINT_COUNT] = {
    {0U, 0U,  32U,  32U},
    {0U, 0U, 287U,  32U},
    {0U, 0U, 287U, 207U},
    {0U, 0U,  32U, 207U},
    {0U, 0U, 160U, 120U}
};

static uint8_t show_target(uint8_t index)
{
    const TouchCalPoint *target = &s_targets[index];
    if (ILI9341_FillScreen(ILI9341_BLACK) != ILI9341_OK) return 0U;
    if (ILI9341_DrawString(12U, 8U, "TOUCH CALIBRATION (5PT)",
                            ILI9341_WHITE, ILI9341_BLACK, 1U) != ILI9341_OK)
        return 0U;
    if (ILI9341_DrawString(12U, 225U, "HOLD TARGET THEN RELEASE",
                            ILI9341_WHITE, ILI9341_BLACK, 1U) != ILI9341_OK)
        return 0U;
    uint16_t x = target->screen_x, y = target->screen_y;
    if (ILI9341_DrawHLine((uint16_t)(x - 10U), y, 21U, ILI9341_YELLOW)
          != ILI9341_OK) return 0U;
    if (ILI9341_DrawVLine(x, (uint16_t)(y - 10U), 21U, ILI9341_YELLOW)
          != ILI9341_OK) return 0U;
    if (ILI9341_FillRect((uint16_t)(x - 2U), (uint16_t)(y - 2U),
                         5U, 5U, ILI9341_RED) != ILI9341_OK) return 0U;
    return 1U;
}

/* Capture one stable contact after release; avoids accepting raw samples that
 * were taken before the target was displayed.  Bound execution even when the
 * HAL tick stalls by using Cortex-M4's independent DWT cycle counter.
 */
static uint8_t capture_point(uint8_t index, TouchCalPoint *result)
{
    uint16_t xs[TOUCH_CAL_MAX_SAMPLES], ys[TOUCH_CAL_MAX_SAMPLES];
    uint8_t count = 0U, saw_down = 0U;
    uint32_t last_seen = touch_raw_stats.samples_ok;
    uint32_t start_ms = HAL_GetTick();
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t clock = SystemCoreClock;
    if (clock == 0U || clock > 100000000U) clock = 50000000U;
    uint32_t cycle_limit = clock * (TOUCH_CAL_POINT_TIMEOUT_MS / 1000U);
    while ((uint32_t)(HAL_GetTick() - start_ms) < TOUCH_CAL_POINT_TIMEOUT_MS &&
           (uint32_t)(DWT->CYCCNT - start_cycles) < cycle_limit)
    {
        TouchRaw_Service(HAL_GetTick());
        uint8_t down = touch_raw_stats.pressed;
        uint32_t latest = touch_raw_stats.samples_ok;
        if (down) {
            saw_down = 1U;
            if (latest != last_seen) {
                last_seen = latest;
                if (count < TOUCH_CAL_MAX_SAMPLES) {
                    xs[count] = touch_raw_stats.raw_x;
                    ys[count] = touch_raw_stats.raw_y;
                    count++;
                    touch_cal_debug.samples_seen++;
                }
            }
        } else if (saw_down) {
            uint16_t minx = 4095U, miny = 4095U, maxx = 0U, maxy = 0U;
            uint32_t sumx = 0U, sumy = 0U;
            for (uint8_t i = 0U; i < count; ++i) {
                uint16_t x = xs[i], y = ys[i];
                if (x < minx) minx = x;
                if (y < miny) miny = y;
                if (x > maxx) maxx = x;
                if (y > maxy) maxy = y;
                sumx += x; sumy += y;
            }
            if (count >= TOUCH_CAL_MIN_SAMPLES &&
                (uint16_t)(maxx - minx) <= TOUCH_CAL_MAX_RAW_JITTER &&
                (uint16_t)(maxy - miny) <= TOUCH_CAL_MAX_RAW_JITTER)
            {
                *result = s_targets[index];
                result->raw_x = (uint16_t)(sumx / count);
                result->raw_y = (uint16_t)(sumy / count);
                return 1U;
            }
            touch_cal_debug.rejected_contacts++;
            saw_down = 0U;
            count = 0U;
        }
    }
    return 0U;
}

void TouchCal_RunBoot(void)
{
    touch_calibration = (TouchCalibration){0};
    touch_cal_debug = (TouchCalDebug){0};
    touch_cal_debug.result = TOUCH_CAL_RUNNING;
    if (!touch_raw_stats.initialized || ILI9341_DMA_IsBusy() ||
        ILI9341_DMA_HasError()) {
        touch_cal_debug.result = TOUCH_CAL_DRAW_ERROR;
        return;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    TouchCalPoint captured[TOUCH_CAL_POINT_COUNT];
    for (uint8_t i = 0U; i < TOUCH_CAL_POINT_COUNT; ++i) {
        touch_cal_debug.point_index = i;
        if (!show_target(i)) {
            touch_cal_debug.result = TOUCH_CAL_DRAW_ERROR;
            return;
        }
        if (!capture_point(i, &captured[i])) {
            touch_cal_debug.result = TOUCH_CAL_TIMEOUT;
            return;
        }
    }
    uint16_t max_error = 0U;
    if (!TouchCal_SolveFive(captured, &touch_calibration, &max_error)) {
        touch_cal_debug.result = TOUCH_CAL_BAD_FIT;
        return;
    }
    touch_cal_debug.max_error_px = max_error;
    touch_cal_debug.calibrated = 1U;
    touch_cal_debug.result = TOUCH_CAL_PASS;
}

void TouchCal_Service(void)
{
    static uint32_t last_seen;
    if (!touch_calibration.valid || !touch_raw_stats.pressed) return;
    uint32_t count = touch_raw_stats.samples_ok;
    if (count == last_seen) return;
    last_seen = count;
    uint16_t x, y;
    uint16_t rx = touch_raw_stats.raw_x, ry = touch_raw_stats.raw_y;
    if (!TouchCal_Map(&touch_calibration, rx, ry, &x, &y)) {
        touch_cal_debug.mapping_failures++;
        return;
    }
    touch_cal_debug.raw_x = rx;
    touch_cal_debug.raw_y = ry;
    touch_cal_debug.screen_x = x;
    touch_cal_debug.screen_y = y;
    touch_cal_debug.mapped_samples++;
}
