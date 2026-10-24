/* M2.1b: hardware-independent touch contact -> TAP recognizer.
 * Coordinates must be calibrated to the 320x240 landscape display first.
 * Foreground-only; do not call from DMA ISR or concurrent RTOS threads.
 */
#ifndef M2_TOUCH_GESTURE_H_
#define M2_TOUCH_GESTURE_H_

#include <stdint.h>

#define M2_TOUCH_WIDTH           320U
#define M2_TOUCH_HEIGHT          240U
#define M2_TOUCH_MIN_PRESS_MS     25U
#define M2_TOUCH_MAX_PRESS_MS    600U
#define M2_TOUCH_MAX_MOVE_PX      12U

typedef enum {
    M2_GESTURE_IDLE = 0,
    M2_GESTURE_TRACKING,
    M2_GESTURE_CANCELLED
} M2_GestureState;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint32_t tick_ms;
} M2_GestureTap;

typedef struct {
    M2_GestureState state;
    uint16_t down_x;
    uint16_t down_y;
    uint32_t down_tick;
    uint32_t samples;
    uint32_t taps;
    uint32_t rejected_short;
    uint32_t rejected_long;
    uint32_t rejected_move;
    uint32_t rejected_bounds;
} M2_Gesture;

void M2_Gesture_Init(M2_Gesture *gesture);
/* Return 1 ONLY on valid contact release and fill *tap.
 * x/y on release are ignored, since some controllers stop reporting them.
 */
uint8_t M2_Gesture_Feed(M2_Gesture *gesture, uint8_t pressed,
                        uint16_t x, uint16_t y, uint32_t now_ms,
                        M2_GestureTap *tap);

#endif /* M2_TOUCH_GESTURE_H_ */
