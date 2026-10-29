/* M2.1c - opt-in XPT2046 / ADS7846-compatible raw touch bring-up.
 * The U2 chip marking has not yet been verified; this backend is provisional.
 * Pin mapping: PC6 CLK, PC7 CS, PC8 DIN, PC9 DO, PC10 IRQ.
 * All functions are foreground-only. GPIO software SPI is isolated from TFT SPI2 DMA.
 */
#ifndef TOUCH_RAW_H_
#define TOUCH_RAW_H_

#include <stdint.h>

typedef struct {
    uint32_t polls;
    uint32_t samples_ok;
    uint32_t pen_down_edges;
    uint32_t pen_up_edges;
    uint32_t invalid_samples;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t min_x;
    uint16_t max_x;
    uint16_t min_y;
    uint16_t max_y;
    uint8_t pressed;
    uint8_t initialized;
} TouchRawStats;

extern volatile TouchRawStats touch_raw_stats;

/* Initializes PC6..PC10 ONLY when called (off by default in main.c). */
void TouchRaw_Init(void);
/* Poll every ~20 ms from main. Raw values are not mapped to screen coordinates. */
void TouchRaw_Service(uint32_t now_ms);

#endif /* TOUCH_RAW_H_ */
