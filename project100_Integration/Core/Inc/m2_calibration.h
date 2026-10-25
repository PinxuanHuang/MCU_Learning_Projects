#ifndef M2_CALIBRATION_H_
#define M2_CALIBRATION_H_

#include <stdint.h>

/* 320x240 screen coordinate space AFTER ILI9341_SetRotation(3). */
#define M2_CAL_WIDTH  320U
#define M2_CAL_HEIGHT 240U
#define M2_CAL_POINT_COUNT 5U

typedef struct {
    uint16_t raw_x, raw_y;
    uint16_t screen_x, screen_y;
} M2_CalPoint;

typedef struct {
    /* Screen coordinates = affine transform of raw ADC readings.
     * x = a*raw_x + b*raw_y + c, y = d*raw_x + e*raw_y + f.
     * Values are held in RAM; no automatic flash writes.
     */
    double a, b, c, d, e, f;
    uint8_t valid;
} M2_Calibration;

typedef enum {
    M2_CAL_NOT_RUN = 0U,
    M2_CAL_RUNNING = 1U,
    M2_CAL_PASS = 2U,
    M2_CAL_TIMEOUT = 3U,
    M2_CAL_BAD_FIT = 4U,
    M2_CAL_DRAW_ERROR = 5U
} M2_CalResult;

typedef struct {
    uint32_t samples_seen;
    uint32_t rejected_contacts;
    uint32_t mapped_samples;
    uint32_t mapping_failures;
    uint16_t raw_x, raw_y;
    uint16_t screen_x, screen_y;
    uint16_t point_index;      /* 0..4 while calibrating */
    uint16_t max_error_px;    /* max fitting error, rounded up */
    uint8_t result;           /* M2_CalResult */
    uint8_t calibrated;
} M2_CalDebug;

extern M2_Calibration m2_calibration;
extern volatile M2_CalDebug m2_cal_debug;

/* Pure C functions; suitable for host-side unit tests. */
uint8_t M2_Cal_SolveFive(const M2_CalPoint points[M2_CAL_POINT_COUNT],
                         M2_Calibration *out, uint16_t *max_error_px);
uint8_t M2_Cal_Map(const M2_Calibration *cal, uint16_t raw_x,
                   uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y);

/* Boot-only interactive five-point acquisition. Run before Dashboard_Init().
 * It uses blocking LCD drawing only while TFT DMA is idle.  It times out,
 * restores normal flow, and never writes MCU flash.
 */
void M2_Cal_RunBoot(void);
/* Foreground-only diagnostic mapping; does not inject touch events into UI. */
void M2_Cal_Service(void);

#endif /* M2_CALIBRATION_H_ */
