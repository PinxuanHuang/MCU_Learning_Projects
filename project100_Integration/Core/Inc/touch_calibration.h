#ifndef TOUCH_CALIBRATION_H_
#define TOUCH_CALIBRATION_H_

#include <stdint.h>

/* 320x240 screen coordinate space AFTER ILI9341_SetRotation(3). */
#define TOUCH_CAL_WIDTH  320U
#define TOUCH_CAL_HEIGHT 240U
#define TOUCH_CAL_POINT_COUNT 5U

typedef struct {
    uint16_t raw_x, raw_y;
    uint16_t screen_x, screen_y;
} TouchCalPoint;

typedef struct {
    /* Screen coordinates = affine transform of raw ADC readings.
     * x = a*raw_x + b*raw_y + c, y = d*raw_x + e*raw_y + f.
     * Values are held in RAM; no automatic flash writes.
     */
    double a, b, c, d, e, f;
    uint8_t valid;
} TouchCalibration;

typedef enum {
    TOUCH_CAL_NOT_RUN = 0U,
    TOUCH_CAL_RUNNING = 1U,
    TOUCH_CAL_PASS = 2U,
    TOUCH_CAL_TIMEOUT = 3U,
    TOUCH_CAL_BAD_FIT = 4U,
    TOUCH_CAL_DRAW_ERROR = 5U
} TouchCalResult;

typedef struct {
    uint32_t samples_seen;
    uint32_t rejected_contacts;
    uint32_t mapped_samples;
    uint32_t mapping_failures;
    uint16_t raw_x, raw_y;
    uint16_t screen_x, screen_y;
    uint16_t point_index;      /* 0..4 while calibrating */
    uint16_t max_error_px;    /* max fitting error, rounded up */
    uint8_t result;           /* TouchCalResult */
    uint8_t calibrated;
} TouchCalDebug;

extern TouchCalibration touch_calibration;
extern volatile TouchCalDebug touch_cal_debug;

/* Pure C functions; suitable for host-side unit tests. */
uint8_t TouchCal_SolveFive(const TouchCalPoint points[TOUCH_CAL_POINT_COUNT],
                         TouchCalibration *out, uint16_t *max_error_px);
uint8_t TouchCal_Map(const TouchCalibration *cal, uint16_t raw_x,
                   uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y);

/* Boot-only interactive five-point acquisition. Run before Dashboard_Init().
 * It uses blocking LCD drawing only while TFT DMA is idle.  It times out,
 * restores normal flow, and never writes MCU flash.
 */
void TouchCal_RunBoot(void);
/* Foreground-only diagnostic mapping; does not inject touch events into UI. */
void TouchCal_Service(void);

#endif /* TOUCH_CALIBRATION_H_ */
