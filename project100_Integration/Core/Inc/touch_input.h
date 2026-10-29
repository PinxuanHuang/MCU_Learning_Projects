/* M2.1 - hardware-independent tap events; B1/PA0 is a simulator backend.
 * The LCD is not assumed to have a touch controller.
 */
#ifndef TOUCH_INPUT_H_
#define TOUCH_INPUT_H_

#include <stdint.h>

typedef enum {
    TOUCH_INPUT_SOURCE_BUTTON_SIM = 1U,
    TOUCH_INPUT_SOURCE_TOUCH = 2U
} TouchInputSource;

typedef enum {
    TOUCH_INPUT_TARGET_NONE = 0U,
    TOUCH_INPUT_TARGET_RX,
    TOUCH_INPUT_TARGET_TX,
    TOUCH_INPUT_TARGET_CPU,
    TOUCH_INPUT_TARGET_RAM
} TouchInputTarget;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint32_t tick_ms;
    TouchInputSource source;
} TouchInputEvent;

typedef struct {
    uint32_t button_presses;
    uint32_t events_queued;
    uint32_t events_popped;
    uint32_t events_dropped;
} TouchInputStats;

extern volatile TouchInputStats touch_input_stats;

/* Foreground-only: configure DISC1 B1 USER button (PA0), active high. */
void TouchInput_Init(uint32_t now_ms);
void TouchInput_Service(uint32_t now_ms);
uint8_t TouchInput_Pop(TouchInputEvent *out);
/* Future real-touch backend may call this after its own SPI/calibration work.
 * Do not call from IRQ or concurrent RTOS tasks; no hardware controller is
 * driven by this API in M2.1.
 */
void TouchInput_InjectTouchTap(uint16_t x, uint16_t y, uint32_t now_ms);
/* New M2.1b route: feed debounced, calibrated contact samples.
 * A TAP is queued on a valid release; return 1 if an event was queued.
 * Coordinates on release are ignored. B1 simulator uses same recognizer.
 */
uint8_t TouchInput_FeedTouchSample(uint8_t pressed, uint16_t x,
                                 uint16_t y, uint32_t now_ms);
TouchInputTarget TouchInput_Classify(const TouchInputEvent *event);

#endif /* TOUCH_INPUT_H_ */
