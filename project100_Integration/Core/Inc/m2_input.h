/* M2.1 - hardware-independent tap events; B1/PA0 is a simulator backend.
 * The LCD is not assumed to have a touch controller.
 */
#ifndef M2_INPUT_H_
#define M2_INPUT_H_

#include <stdint.h>

typedef enum {
    M2_INPUT_SOURCE_BUTTON_SIM = 1U,
    M2_INPUT_SOURCE_TOUCH = 2U
} M2_InputSource;

typedef enum {
    M2_INPUT_TARGET_NONE = 0U,
    M2_INPUT_TARGET_RX,
    M2_INPUT_TARGET_TX,
    M2_INPUT_TARGET_CPU,
    M2_INPUT_TARGET_RAM
} M2_InputTarget;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint32_t tick_ms;
    M2_InputSource source;
} M2_InputEvent;

typedef struct {
    uint32_t button_presses;
    uint32_t events_queued;
    uint32_t events_popped;
    uint32_t events_dropped;
} M2_InputStats;

extern volatile M2_InputStats m2_input_stats;

/* Foreground-only: configure DISC1 B1 USER button (PA0), active high. */
void M2_Input_Init(uint32_t now_ms);
void M2_Input_Service(uint32_t now_ms);
uint8_t M2_Input_Pop(M2_InputEvent *out);
/* Future real-touch backend may call this after its own SPI/calibration work.
 * Do not call from IRQ or concurrent RTOS tasks; no hardware controller is
 * driven by this API in M2.1.
 */
void M2_Input_InjectTouchTap(uint16_t x, uint16_t y, uint32_t now_ms);
/* New M2.1b route: feed debounced, calibrated contact samples.
 * A TAP is queued on a valid release; return 1 if an event was queued.
 * Coordinates on release are ignored. B1 simulator uses same recognizer.
 */
uint8_t M2_Input_FeedTouchSample(uint8_t pressed, uint16_t x,
                                 uint16_t y, uint32_t now_ms);
M2_InputTarget M2_Input_Classify(const M2_InputEvent *event);

#endif /* M2_INPUT_H_ */
