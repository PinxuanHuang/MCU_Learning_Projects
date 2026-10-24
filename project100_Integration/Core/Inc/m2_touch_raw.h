/* M2.1c - opt-in XPT2046 / ADS7846-compatible raw touch bring-up.
 * The U2 chip marking has not yet been verified; this backend is provisional.
 * Pin mapping: PC6 CLK, PC7 CS, PC8 DIN, PC9 DO, PC10 IRQ.
 * All functions are foreground-only. GPIO software SPI is isolated from TFT SPI2 DMA.
 */
#ifndef M2_TOUCH_RAW_H_
#define M2_TOUCH_RAW_H_

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
} M2_TouchRawStats;

extern volatile M2_TouchRawStats m2_touch_raw_stats;

/* Initializes PC6..PC10 ONLY when called (off by default in main.c). */
void M2_TouchRaw_Init(void);
/* Poll every ~20 ms from main. Raw values are not mapped to screen coordinates. */
void M2_TouchRaw_Service(uint32_t now_ms);

#endif /* M2_TOUCH_RAW_H_ */
